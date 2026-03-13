/*
 * @FilePath: /src/utils/net/configuredServer.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Config-driven server bootstrap with plugin loading
 */

#include "net/configuredServer.h"

#include <dlfcn.h>

#include <memory>
#include <unordered_map>
#include <utility>

namespace utils {
namespace net {

namespace {

struct PluginDeleter {
    DestroyNetPluginFn destroy{nullptr};

    void operator()(NetPlugin* plugin) const {
        if (!plugin) return;
        if (destroy) {
            destroy(plugin);
            return;
        }
        delete plugin;
    }
};

using StoredPlugin = std::unique_ptr<NetPlugin, PluginDeleter>;
using HandlerMap = std::unordered_map<std::string, HttpHandlerRegistrar::Handler>;

} // namespace

struct ConfiguredServer::Impl {
    struct LoadedPlugin {
        void* libraryHandle{nullptr};
        PluginContext context;
        StoredPlugin plugin{nullptr, PluginDeleter{}};
    };

    std::unordered_map<std::string, HandlerMap> handlers;
    std::vector<LoadedPlugin> plugins;
};

ConfiguredServer::ConfiguredServer(RuntimeConfig config)
    : impl_(new Impl())
    , config_(std::move(config))
    , server_(new Server(config_.server)) {}

ConfiguredServer::~ConfiguredServer() {
    stop();
    join();
    if (!impl_) return;
    for (auto& plugin : impl_->plugins) {
        plugin.plugin.reset();
        if (plugin.libraryHandle) {
            ::dlclose(plugin.libraryHandle);
            plugin.libraryHandle = nullptr;
        }
    }
}

std::unique_ptr<ConfiguredServer> ConfiguredServer::createFromFile(const std::string& configPath,
                                                                   std::string* error) {
    RuntimeConfig config;
    std::string localError;
    if (!loadRuntimeConfig(configPath, config, localError)) {
        if (error) *error = localError;
        return nullptr;
    }

    std::unique_ptr<ConfiguredServer> server(new ConfiguredServer(std::move(config)));
    if (!server->build(localError)) {
        if (error) *error = localError;
        return nullptr;
    }
    return server;
}

bool ConfiguredServer::build(std::string& error) {
    for (const auto& staticDirectory : config_.staticDirectories) {
        server_->http().staticDir(staticDirectory.urlPrefix, staticDirectory.directory);
    }

    for (const auto& pluginConfig : config_.plugins) {
        void* handle = ::dlopen(pluginConfig.libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            error = "Failed to load plugin library '" + pluginConfig.libraryPath + "': " + ::dlerror();
            return false;
        }

        ::dlerror();
        auto create = reinterpret_cast<CreateNetPluginFn>(::dlsym(handle, kCreateNetPluginSymbol));
        const char* createError = ::dlerror();
        if (!create || createError) {
            error = "Plugin '" + pluginConfig.instanceName + "' is missing symbol '" +
                    std::string(kCreateNetPluginSymbol) + "'";
            ::dlclose(handle);
            return false;
        }

        auto destroy = reinterpret_cast<DestroyNetPluginFn>(::dlsym(handle, kDestroyNetPluginSymbol));
        ::dlerror();

        NetPlugin* rawPlugin = create();
        if (!rawPlugin) {
            error = "Plugin factory returned null for '" + pluginConfig.instanceName + "'";
            ::dlclose(handle);
            return false;
        }

        auto registrarImpl = std::make_shared<HttpHandlerRegistrar::Impl>();
        registrarImpl->instanceName = pluginConfig.instanceName;
        HttpHandlerRegistrar registrar(registrarImpl);

        PluginContext pluginContext;
        pluginContext.instanceName = pluginConfig.instanceName;
        pluginContext.configPath = config_.configPath;
        pluginContext.configDirectory = config_.configDirectory;

        std::string registerError;
        if (!rawPlugin->registerHandlers(registrar, pluginContext, pluginConfig.config, registerError)) {
            PluginDeleter{destroy}(rawPlugin);
            ::dlclose(handle);
            error = "Plugin '" + pluginConfig.instanceName + "' failed to register handlers: " + registerError;
            return false;
        }

        impl_->handlers.emplace(pluginConfig.instanceName, registrarImpl->handlers);

        Impl::LoadedPlugin loadedPlugin;
        loadedPlugin.libraryHandle = handle;
        loadedPlugin.context = std::move(pluginContext);
        loadedPlugin.plugin = StoredPlugin(rawPlugin, PluginDeleter{destroy});
        impl_->plugins.emplace_back(std::move(loadedPlugin));
    }

    for (const auto& route : config_.routes) {
        const auto pluginIt = impl_->handlers.find(route.pluginName);
        if (pluginIt == impl_->handlers.end()) {
            error = "Route '" + route.path + "' references unknown plugin instance '" + route.pluginName + "'";
            return false;
        }
        const auto handlerIt = pluginIt->second.find(route.handlerName);
        if (handlerIt == pluginIt->second.end()) {
            error = "Route '" + route.path + "' references unknown handler '" + route.handlerName +
                    "' in plugin '" + route.pluginName + "'";
            return false;
        }

        auto handler = handlerIt->second;
        if (!server_->http().on(route.method, route.path, std::move(handler))) {
            error = "Failed to register route '" + route.method + " " + route.path + "'";
            return false;
        }
    }

    return true;
}

bool ConfiguredServer::start(std::string* error) {
    if (server_->start()) {
        return true;
    }
    if (error) {
        *error = "Configured server failed to start";
    }
    return false;
}

void ConfiguredServer::stop() {
    server_->stop();
}

void ConfiguredServer::join() {
    server_->join();
}

} // namespace net
} // namespace utils

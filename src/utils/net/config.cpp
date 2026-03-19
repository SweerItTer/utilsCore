/*
 * @FilePath: /src/utils/net/config.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: JSON config loader for utils::net configured services
 */

#include "net/config.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace utils {
namespace net {

namespace {

std::string getDirectoryName(const std::string& path) {
    const auto slashPos = path.find_last_of("/\\");
    if (slashPos == std::string::npos) return ".";
    return path.substr(0, slashPos);
}

bool isAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/') return true;
    return path.size() > 1 && path[1] == ':';
}

std::string joinPath(const std::string& base, const std::string& child) {
    if (child.empty()) return base;
    if (isAbsolutePath(child)) return child;
    if (base.empty() || base == ".") return child;
    if (base.back() == '/') return base + child;
    return base + "/" + child;
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

bool expectObject(const JsonValue& value, const std::string& fieldName, std::string& error) {
    if (value.isObject()) return true;
    error = "Field '" + fieldName + "' must be a JSON object";
    return false;
}

bool expectArray(const JsonValue& value, const std::string& fieldName, std::string& error) {
    if (value.isArray()) return true;
    error = "Field '" + fieldName + "' must be a JSON array";
    return false;
}

bool readStringField(const JsonValue& object,
                     const std::string& fieldName,
                     std::string& outValue,
                     std::string& error,
                     bool required = true) {
    const JsonValue* value = object.find(fieldName);
    if (!value) {
        if (!required) return true;
        error = "Missing required string field '" + fieldName + "'";
        return false;
    }
    if (!value->isString()) {
        error = "Field '" + fieldName + "' must be a string";
        return false;
    }
    outValue = value->asString();
    return true;
}

void applyServerNumber(const JsonValue& object,
                       const char* fieldName,
                       std::function<void(int)> setter) {
    const JsonValue* value = object.find(fieldName);
    if (value && value->isNumber()) {
        setter(value->asInt());
    }
}

void applyServerBool(const JsonValue& object,
                     const char* fieldName,
                     std::function<void(bool)> setter) {
    const JsonValue* value = object.find(fieldName);
    if (value && value->isBool()) {
        setter(value->asBool());
    }
}

} // namespace

bool loadRuntimeConfig(const std::string& configPath, RuntimeConfig& outConfig, std::string& error) {
    JsonValue root;
    if (!JsonValue::parseFile(configPath, root, error)) {
        return false;
    }
    if (!root.isObject()) {
        error = "Top level JSON must be an object";
        return false;
    }

    RuntimeConfig config;
    config.configPath = configPath;
    config.configDirectory = getDirectoryName(configPath);

    const JsonValue* serverObject = root.find("server");
    if (serverObject) {
        if (!expectObject(*serverObject, "server", error)) return false;

        const JsonValue* bindAddress = serverObject->find("bind_address");
        if (bindAddress) {
            if (!bindAddress->isString()) {
                error = "Field 'server.bind_address' must be a string";
                return false;
            }
            config.server.bindAddress = bindAddress->asString();
        }

        applyServerNumber(*serverObject, "port", [&](int value) {
            config.server.port = static_cast<uint16_t>(value);
        });
        applyServerNumber(*serverObject, "max_clients", [&](int value) {
            config.server.maxClients = static_cast<uint32_t>(value);
        });
        applyServerNumber(*serverObject, "io_threads", [&](int value) {
            config.server.ioThreads = static_cast<uint32_t>(value);
        });
        applyServerNumber(*serverObject, "worker_threads_min", [&](int value) {
            config.server.workerThreadsMin = static_cast<uint32_t>(value);
        });
        applyServerNumber(*serverObject, "worker_threads_max", [&](int value) {
            config.server.workerThreadsMax = static_cast<uint32_t>(value);
        });
        applyServerNumber(*serverObject, "worker_queue_size", [&](int value) {
            config.server.workerQueueSize = static_cast<uint32_t>(value);
        });
        applyServerNumber(*serverObject, "idle_timeout_sec", [&](int value) {
            config.server.idleTimeoutSec = value;
        });
        applyServerBool(*serverObject, "enable_tcp_keepalive", [&](bool value) {
            config.server.enableTcpKeepAlive = value;
        });
        applyServerNumber(*serverObject, "tcp_keep_idle", [&](int value) {
            config.server.tcpKeepIdle = value;
        });
        applyServerNumber(*serverObject, "tcp_keep_interval", [&](int value) {
            config.server.tcpKeepInterval = value;
        });
        applyServerNumber(*serverObject, "tcp_keep_count", [&](int value) {
            config.server.tcpKeepCount = value;
        });
    }

    const JsonValue* staticDirs = root.find("static_dirs");
    if (staticDirs) {
        if (!expectArray(*staticDirs, "static_dirs", error)) return false;
        for (size_t index = 0; index < staticDirs->size(); ++index) {
            const JsonValue& entry = (*staticDirs)[index];
            if (!entry.isObject()) {
                error = "Each static_dirs entry must be an object";
                return false;
            }
            StaticDirectoryConfig staticConfig;
            if (!readStringField(entry, "url_prefix", staticConfig.urlPrefix, error)) return false;
            if (!readStringField(entry, "directory", staticConfig.directory, error)) return false;
            staticConfig.directory = joinPath(config.configDirectory, staticConfig.directory);
            config.staticDirectories.emplace_back(std::move(staticConfig));
        }
    }

    const JsonValue* plugins = root.find("plugins");
    if (!plugins || !expectArray(*plugins, "plugins", error)) {
        if (!plugins) {
            error = "Missing required array field 'plugins'";
        }
        return false;
    }
    for (size_t index = 0; index < plugins->size(); ++index) {
        const JsonValue& entry = (*plugins)[index];
        if (!entry.isObject()) {
            error = "Each plugins entry must be an object";
            return false;
        }
        PluginInstanceConfig pluginConfig;
        if (!readStringField(entry, "instance_name", pluginConfig.instanceName, error)) return false;
        if (!readStringField(entry, "library_path", pluginConfig.libraryPath, error)) return false;
        pluginConfig.libraryPath = joinPath(config.configDirectory, pluginConfig.libraryPath);
        if (const JsonValue* pluginSettings = entry.find("config")) {
            pluginConfig.config = *pluginSettings;
        }
        config.plugins.emplace_back(std::move(pluginConfig));
    }

    const JsonValue* routes = root.find("routes");
    if (!routes || !expectArray(*routes, "routes", error)) {
        if (!routes) {
            error = "Missing required array field 'routes'";
        }
        return false;
    }
    for (size_t index = 0; index < routes->size(); ++index) {
        const JsonValue& entry = (*routes)[index];
        if (!entry.isObject()) {
            error = "Each routes entry must be an object";
            return false;
        }
        RouteConfig routeConfig;
        if (!readStringField(entry, "method", routeConfig.method, error)) return false;
        if (!readStringField(entry, "path", routeConfig.path, error)) return false;
        if (!readStringField(entry, "plugin", routeConfig.pluginName, error)) return false;
        if (!readStringField(entry, "handler", routeConfig.handlerName, error)) return false;
        routeConfig.method = toUpper(routeConfig.method);
        config.routes.emplace_back(std::move(routeConfig));
    }

    outConfig = std::move(config);
    return true;
}

} // namespace net
} // namespace utils

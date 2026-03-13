/*
 * @FilePath: /examples/net_demo_plugin.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Demo plugin for utils::net configured HTTP services
 */

#include "net/plugin.h"
#include "net/server.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>

namespace {

std::string joinPath(const std::string& base, const std::string& child) {
    if (child.empty()) return base;
    if (!child.empty() && child.front() == '/') return child;
    if (base.empty() || base == ".") return child;
    if (base.back() == '/') return base + child;
    return base + "/" + child;
}

class DemoPlugin : public utils::net::NetPlugin {
public:
    bool registerHandlers(utils::net::HttpHandlerRegistrar& registrar,
                          const utils::net::PluginContext& context,
                          const utils::net::JsonValue& pluginConfig,
                          std::string& error) override {
        std::string downloadFile = "www/sample.txt";
        if (const utils::net::JsonValue* configuredFile = pluginConfig.find("download_file")) {
            if (!configuredFile->isString()) {
                error = "Plugin field 'download_file' must be a string";
                return false;
            }
            downloadFile = configuredFile->asString();
        }
        downloadFile = joinPath(context.configDirectory, downloadFile);

        if (!registrar.registerHandler("ping",
                [instanceName = context.instanceName](const utils::net::ConnectionContext& ctx,
                                                      const utils::net::HttpRequest&) {
                    utils::net::JsonValue body = utils::net::JsonValue::object();
                    body["ok"] = true;
                    body["plugin"] = instanceName;
                    body["peer"] = utils::net::JsonValue::object();
                    body["peer"]["ip"] = ctx.peer.ip;
                    body["peer"]["port"] = static_cast<int>(ctx.peer.port);
                    return utils::net::HttpResponse::ok().json(body).toResponse();
                }, &error)) {
            return false;
        }

        if (!registrar.registerHandler("echo",
                [](const utils::net::ConnectionContext&, const utils::net::HttpRequest& request) {
                    utils::net::JsonValue payload;
                    std::string message = request.body;
                    std::string parseError;
                    if (!request.body.empty() && request.parseJsonBody(payload, parseError)) {
                        if (const utils::net::JsonValue* value = payload.find("message")) {
                            if (value->isString()) {
                                message = value->asString();
                            }
                        }
                    }

                    utils::net::JsonValue body = utils::net::JsonValue::object();
                    body["message"] = message;
                    body["method"] = request.method;
                    body["path"] = request.path();
                    return utils::net::HttpResponse::ok().json(body).toResponse();
                }, &error)) {
            return false;
        }

        if (!registrar.registerHandler("download",
                [downloadFile](const utils::net::ConnectionContext&, const utils::net::HttpRequest&) {
                    struct stat fileStat {};
                    const int fd = ::open(downloadFile.c_str(), O_RDONLY | O_CLOEXEC);
                    if (fd < 0 || ::fstat(fd, &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
                        if (fd >= 0) ::close(fd);
                        return utils::net::HttpResponse::notFound()
                            .contentType("text/plain; charset=utf-8")
                            .body("download file not found\n")
                            .toResponse();
                    }

                    return utils::net::HttpResponse::ok()
                        .contentType("text/plain; charset=utf-8")
                        .header("Content-Disposition", "attachment; filename=\"sample.txt\"")
                        .bodyFromFileFd(FdWrapper(fd), 0, static_cast<uint64_t>(fileStat.st_size))
                        .toResponse();
                }, &error)) {
            return false;
        }

        return true;
    }
};

} // namespace

extern "C" utils::net::NetPlugin* createNetPlugin() {
    return new DemoPlugin();
}

extern "C" void destroyNetPlugin(utils::net::NetPlugin* plugin) {
    delete plugin;
}

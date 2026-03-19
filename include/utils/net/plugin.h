/*
 * @FilePath: /include/utils/net/plugin.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Plugin interfaces for utils::net configured HTTP services
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "http.h"
#include "json.h"
#include "response.h"

namespace utils {
namespace net {

struct ConnectionContext;

struct PluginContext {
    std::string instanceName;
    std::string configPath;
    std::string configDirectory;
};

class HttpHandlerRegistrar {
public:
    using Handler = std::function<Response(const ConnectionContext&, const HttpRequest&)>;

    /**
     * @brief 注册一个供配置文件绑定的 HTTP handler.
     * @param handlerName 路由声明中使用的 handler 名称
     * @param handler 具体处理函数
     * @param error 失败时写入错误原因
     * @return true 注册成功
     */
    bool registerHandler(std::string handlerName, Handler handler, std::string* error = nullptr);
    const std::string& instanceName() const;

private:
    struct Impl {
        std::string instanceName;
        std::unordered_map<std::string, Handler> handlers;
    };
    explicit HttpHandlerRegistrar(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;

    friend class ConfiguredServer;
};

class NetPlugin {
public:
    virtual ~NetPlugin() = default;

    /**
     * @brief 基于插件级配置注册本插件提供的 handler.
     * @param registrar handler 注册器
     * @param context 当前插件实例上下文
     * @param pluginConfig 插件在 JSON 配置中的 config 字段
     * @param error 注册失败时写入错误原因
     * @return true 注册成功
     */
    virtual bool registerHandlers(HttpHandlerRegistrar& registrar,
                                  const PluginContext& context,
                                  const JsonValue& pluginConfig,
                                  std::string& error) = 0;
};

using CreateNetPluginFn = NetPlugin* (*)();
using DestroyNetPluginFn = void (*)(NetPlugin*);

constexpr const char* kCreateNetPluginSymbol = "createNetPlugin";
constexpr const char* kDestroyNetPluginSymbol = "destroyNetPlugin";

} // namespace net
} // namespace utils

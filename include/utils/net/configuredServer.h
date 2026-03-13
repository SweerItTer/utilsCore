/*
 * @FilePath: /include/utils/net/configuredServer.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Config-driven HTTP service bootstrap for utils::net
 */
#pragma once

#include <memory>
#include <string>

#include "config.h"
#include "plugin.h"
#include "server.h"

namespace utils {
namespace net {

class ConfiguredServer {
public:
    /**
     * @brief 从 JSON 配置文件构建一个已完成路由与插件装载的服务实例。
     * @param configPath JSON 配置文件路径
     * @param error 失败时写入错误原因
     * @return 成功时返回可启动的服务对象
     */
    static std::unique_ptr<ConfiguredServer> createFromFile(const std::string& configPath,
                                                            std::string* error = nullptr);

    ~ConfiguredServer();

    ConfiguredServer(const ConfiguredServer&) = delete;
    ConfiguredServer& operator=(const ConfiguredServer&) = delete;

    bool start(std::string* error = nullptr);
    void stop();
    void join();

    Server& server() { return *server_; }
    const RuntimeConfig& config() const { return config_; }

private:
    explicit ConfiguredServer(RuntimeConfig config);
    bool build(std::string& error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    RuntimeConfig config_;
    std::unique_ptr<Server> server_;
};

} // namespace net
} // namespace utils

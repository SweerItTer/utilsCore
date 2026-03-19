/*
 * @FilePath: /include/utils/net/config.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: JSON runtime configuration for utils::net configured services
 */
#pragma once

#include <string>
#include <vector>

#include "json.h"
#include "server.h"

namespace utils {
namespace net {

struct StaticDirectoryConfig {
    std::string urlPrefix;
    std::string directory;
};

struct PluginInstanceConfig {
    std::string instanceName;
    std::string libraryPath;
    JsonValue config{JsonValue::object()};
};

struct RouteConfig {
    std::string method;
    std::string path;
    std::string pluginName;
    std::string handlerName;
};

struct RuntimeConfig {
    std::string configPath;
    std::string configDirectory;
    ServerConfig server;
    std::vector<StaticDirectoryConfig> staticDirectories;
    std::vector<PluginInstanceConfig> plugins;
    std::vector<RouteConfig> routes;
};

/**
 * @brief 从 JSON 文件加载 net 运行时配置.
 * @param configPath JSON 配置文件路径
 * @param outConfig 解析成功后写入的配置对象
 * @param error 失败时写入错误原因
 * @return true 解析成功
 */
bool loadRuntimeConfig(const std::string& configPath, RuntimeConfig& outConfig, std::string& error);

} // namespace net
} // namespace utils

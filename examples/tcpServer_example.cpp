/*
 * @FilePath: /examples/tcpServer_example.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Config-driven HTTP demo for utils::net configured server + plugins
 */

#include <csignal>
#include <chrono>
#include <thread>

#include "logger_config.h"
#include "logger_v2.h"
#include "net/configuredServer.h"

namespace {

volatile std::sig_atomic_t gShouldStop = 0;

void handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        gShouldStop = 1;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string configPath = (argc >= 2) ? argv[1] : "net_demo.json";
    utils::LoggerConfig loggerConfig = utils::LoggerConfig::defaultConfig();
    loggerConfig.async = false;
    loggerConfig.global_level = utils::LogLevel::INFO;
    utils::LoggerV2::init(loggerConfig);

    std::string error;
    auto server = utils::net::ConfiguredServer::createFromFile(configPath, &error);
    if (!server) {
        LOG_ERROR("Failed to load config: %s", error.c_str());
        return 1;
    }

    if (!server->start(&error)) {
        LOG_ERROR("Failed to start configured server: %s", error.c_str());
        return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const auto& runtimeConfig = server->config();
    LOG_INFO("========================================");
    LOG_INFO(" utils::net Configured HTTP Demo");
    LOG_INFO("========================================");
    LOG_INFO("Config : %s", runtimeConfig.configPath.c_str());
    LOG_INFO("Listen : %s:%d", runtimeConfig.server.bindAddress.c_str(), runtimeConfig.server.port);
    LOG_INFO("Try    : curl http://127.0.0.1:%d/api/ping", runtimeConfig.server.port);
    LOG_INFO("Try    : curl -X POST http://127.0.0.1:%d/api/echo -H 'Content-Type: application/json' -d '{\"message\":\"Hello\"}'",
             runtimeConfig.server.port);
    LOG_INFO("Try    : curl http://127.0.0.1:%d/static/index.html", runtimeConfig.server.port);
    LOG_INFO("Try    : curl -OJ http://127.0.0.1:%d/download/sample.txt", runtimeConfig.server.port);
    LOG_INFO("Press Ctrl+C to stop.");

    while (!gShouldStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->stop();
    server->join();
    utils::LoggerV2::shutdown();
    return 0;
}

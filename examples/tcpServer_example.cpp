/*
 * @FilePath: /examples/tcpServer_example.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Config-driven HTTP demo for utils::net configured server + plugins
 */

#include <csignal>
#include <chrono>
#include <iostream>
#include <thread>

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

    std::string error;
    auto server = utils::net::ConfiguredServer::createFromFile(configPath, &error);
    if (!server) {
        std::cerr << "Failed to load config: " << error << "\n";
        return 1;
    }

    if (!server->start(&error)) {
        std::cerr << "Failed to start configured server: " << error << "\n";
        return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const auto& runtimeConfig = server->config();
    std::cout << "========================================\n";
    std::cout << " utils::net Configured HTTP Demo\n";
    std::cout << "========================================\n";
    std::cout << "Config : " << runtimeConfig.configPath << "\n";
    std::cout << "Listen : " << runtimeConfig.server.bindAddress << ":" << runtimeConfig.server.port << "\n";
    std::cout << "Try    : curl http://127.0.0.1:" << runtimeConfig.server.port << "/api/ping\n";
    std::cout << "Try    : curl -X POST http://127.0.0.1:" << runtimeConfig.server.port
              << "/api/echo -H 'Content-Type: application/json' -d '{\"message\":\"Hello\"}'\n";
    std::cout << "Try    : curl http://127.0.0.1:" << runtimeConfig.server.port << "/static/index.html\n";
    std::cout << "Try    : curl -OJ http://127.0.0.1:" << runtimeConfig.server.port << "/download/sample.txt\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (!gShouldStop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server->stop();
    server->join();
    return 0;
}

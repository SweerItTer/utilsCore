/*
 * @FilePath: /include/utils/net/server.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: utils::net - callback-driven, epoll-based TCP server foundation
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "asyncThreadPool.h"
#include "http.h"
#include "line.h"
#include "response.h"

namespace utils {
namespace net {

struct ServerConfig {
    std::string bindAddress{"0.0.0.0"};
    uint16_t port{8080};
    uint32_t maxClients{128};

    // IO and worker threading
    uint32_t ioThreads{1}; // v1 uses 1
    uint32_t workerThreadsMin{2};
    uint32_t workerThreadsMax{8};
    uint32_t workerQueueSize{128};

    // Connection management
    int idleTimeoutSec{15};

    // TCP keepalive (socket options)
    bool enableTcpKeepAlive{true};
    int tcpKeepIdle{60};
    int tcpKeepInterval{10};
    int tcpKeepCount{5};
};

struct PeerInfo {
    std::string ip;
    uint16_t port{0};
};

struct ConnectionContext {
    uint64_t id{0};
    PeerInfo peer;
};

using LineHandler = std::function<Response(const ConnectionContext&, const LineRequest&)>;
using HttpHandler = std::function<Response(const ConnectionContext&, const HttpRequest&)>;

class LineRouter {
public:
    bool on(std::string command, LineHandler handler);
    Response dispatch(const ConnectionContext& ctx, const LineRequest& req) const;

private:
    std::unordered_map<std::string, LineHandler> handlers_;
};

class HttpRouter {
public:
    using MethodHandlers = std::unordered_map<std::string, HttpHandler>;

    bool on(std::string method, std::string path, HttpHandler handler);
    bool get(std::string path, HttpHandler handler);
    bool head(std::string path, HttpHandler handler);
    bool post(std::string path, HttpHandler handler);
    bool put(std::string path, HttpHandler handler);
    bool del(std::string path, HttpHandler handler);
    bool patch(std::string path, HttpHandler handler);
    bool options(std::string path, HttpHandler handler);

    // Serve a whole directory under a URL prefix. Example:
    // staticDir("/static/", "www");
    void staticDir(std::string urlPrefix, std::string directory);

    Response dispatch(const ConnectionContext& ctx, const HttpRequest& req) const;

private:
    struct StaticDir {
        std::string prefix;
        std::string dir;
    };

    std::unordered_map<std::string, MethodHandlers> handlers_;
    std::vector<StaticDir> staticDirs_;
};

class Server {
public:
    explicit Server(ServerConfig cfg);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void stop();
    void join();

    bool isRunning() const { return running_.load(); }

    LineRouter& line() { return lineRouter_; }
    HttpRouter& http() { return httpRouter_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
    ServerConfig cfg_;
    LineRouter lineRouter_;
    HttpRouter httpRouter_;
    std::unique_ptr<asyncThreadPool> workers_;
};

} // namespace net
} // namespace utils

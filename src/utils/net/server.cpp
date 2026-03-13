/*
 * @FilePath: /src/utils/net/server.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: epoll-based NET server core implementation
 */

#include "net/server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__has_include)
#  if __has_include("dma/dmaBuffer.h") && __has_include(<drm.h>)
#    include "dma/dmaBuffer.h"
#    define UTILSCORE_NET_HAS_DMABUF 1
#  else
#    define UTILSCORE_NET_HAS_DMABUF 0
#  endif
#else
// If compiler doesn't support __has_include, assume target environment provides dependencies.
#  define UTILSCORE_NET_HAS_DMABUF 1
#  include "dma/dmaBuffer.h"
#endif

namespace utils {
namespace net {

static bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void setTcpKeepAliveOptions(int fd, const ServerConfig& cfg) {
    if (!cfg.enableTcpKeepAlive) return;
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

#ifdef TCP_KEEPIDLE
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &cfg.tcpKeepIdle, sizeof(cfg.tcpKeepIdle));
#endif
#ifdef TCP_KEEPINTVL
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &cfg.tcpKeepInterval, sizeof(cfg.tcpKeepInterval));
#endif
#ifdef TCP_KEEPCNT
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cfg.tcpKeepCount, sizeof(cfg.tcpKeepCount));
#endif
}

static bool sendAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false; // try later
        return false;
    }
    return true;
}

static bool looksLikeHttp(const std::string& s) {
    // Very small heuristic: method SP path SP HTTP/1.
    const char* methods[] = {"GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ", "PATCH "};
    for (const char* m : methods) {
        if (s.rfind(m, 0) == 0) {
            if (s.find(" HTTP/1.") != std::string::npos) return true;
        }
    }
    return false;
}

static std::string toLower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

static std::string trim(std::string s) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static std::string stripQuery(std::string target) {
    const auto q = target.find('?');
    if (q != std::string::npos) target.resize(q);
    return target;
}

static std::string normalizeMethod(std::string method) {
    for (auto& ch : method) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return method;
}

static bool isSafeRelativePath(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel[0] == '/' || rel.find('\\') != std::string::npos) return false;
    if (rel.find("..") != std::string::npos) return false;
    return true;
}

static std::string guessContentType(const std::string& path) {
    auto dot = path.find_last_of('.');
    const std::string ext = (dot == std::string::npos) ? "" : toLower(path.substr(dot + 1));
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool LineRouter::on(std::string command, LineHandler handler) {
    if (command.empty() || !handler) return false;
    handlers_[std::move(command)] = std::move(handler);
    return true;
}

Response LineRouter::dispatch(const ConnectionContext& ctx, const LineRequest& req) const {
    auto it = handlers_.find(req.command);
    if (it == handlers_.end()) {
        return Response::bytes("ERROR: Command not exist\n");
    }
    return it->second(ctx, req);
}

bool HttpRouter::on(std::string method, std::string path, HttpHandler handler) {
    if (method.empty() || path.empty() || !handler) return false;
    MethodHandlers& methodHandlers = handlers_[normalizeMethod(std::move(method))];
    const auto inserted = methodHandlers.emplace(std::move(path), std::move(handler));
    return inserted.second;
}

bool HttpRouter::get(std::string path, HttpHandler handler) {
    return on("GET", std::move(path), std::move(handler));
}

bool HttpRouter::head(std::string path, HttpHandler handler) {
    return on("HEAD", std::move(path), std::move(handler));
}

bool HttpRouter::post(std::string path, HttpHandler handler) {
    return on("POST", std::move(path), std::move(handler));
}

bool HttpRouter::put(std::string path, HttpHandler handler) {
    return on("PUT", std::move(path), std::move(handler));
}

bool HttpRouter::del(std::string path, HttpHandler handler) {
    return on("DELETE", std::move(path), std::move(handler));
}

bool HttpRouter::patch(std::string path, HttpHandler handler) {
    return on("PATCH", std::move(path), std::move(handler));
}

bool HttpRouter::options(std::string path, HttpHandler handler) {
    return on("OPTIONS", std::move(path), std::move(handler));
}

static bool pathExistsInAnyMethod(const std::unordered_map<std::string, HttpRouter::MethodHandlers>& handlers,
                                  const std::string& path) {
    for (const auto& entry : handlers) {
        if (entry.second.find(path) != entry.second.end()) {
            return true;
        }
    }
    return false;
}

void HttpRouter::staticDir(std::string urlPrefix, std::string directory) {
    if (urlPrefix.empty()) return;
    if (urlPrefix != "/" && urlPrefix.back() != '/') urlPrefix.push_back('/');
    staticDirs_.push_back(StaticDir{std::move(urlPrefix), std::move(directory)});
}

static Response serveStaticFile(const HttpRequest& req, const std::string& fullPath, bool keepAlive) {
    struct stat st {};
    const int fd = ::open(fullPath.c_str(), O_RDONLY);
    if (fd < 0) return HttpResponse::notFound().keepAlive(keepAlive).toResponse();
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return HttpResponse::notFound().keepAlive(keepAlive).toResponse();
    }

    HttpResponse resp = HttpResponse::ok();
    resp.keepAlive(keepAlive);
    resp.contentType(guessContentType(fullPath));
    resp.bodyFromFileFd(FdWrapper(fd), 0, static_cast<uint64_t>(st.st_size));
    return resp.toResponse();
}

Response HttpRouter::dispatch(const ConnectionContext& ctx, const HttpRequest& req) const {
    (void)ctx;

    const auto connIt = req.headers.find("connection");
    const bool keepAlive = !(connIt != req.headers.end() && toLower(connIt->second) == "close");

    const std::string method = normalizeMethod(req.method);
    const std::string targetPath = stripQuery(req.target);

    const auto tableIt = handlers_.find(method);
    if (tableIt != handlers_.end()) {
        const auto handlerIt = tableIt->second.find(targetPath);
        if (handlerIt != tableIt->second.end()) {
            return handlerIt->second(ctx, req);
        }
    }
    if (pathExistsInAnyMethod(handlers_, targetPath)) {
        return HttpResponse::methodNotAllowed().keepAlive(keepAlive).toResponse();
    }

    // static dirs
    std::string target = targetPath;
    for (const auto& s : staticDirs_) {
        if (target.rfind(s.prefix, 0) != 0) continue;
        if (method != "GET" && method != "HEAD") {
            return HttpResponse::methodNotAllowed().keepAlive(keepAlive).toResponse();
        }
        std::string rel = target.substr(s.prefix.size());
        if (rel.empty()) rel = "index.html";
        if (!isSafeRelativePath(rel)) return HttpResponse::badRequest().keepAlive(keepAlive).toResponse();

        std::string full = s.dir;
        if (!full.empty() && full.back() != '/') full.push_back('/');
        full += rel;
        Response response = serveStaticFile(req, full, keepAlive);
        if (method == "HEAD") {
            response.body = Body::empty();
        }
        return response;
    }

    return HttpResponse::notFound().keepAlive(keepAlive).toResponse();
}

class Server::Impl {
public:
    explicit Impl(Server& owner) : owner_(owner) {}

    bool start() {
        if (thread_.joinable()) return false;

        const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) return false;

        FdWrapper listenWrap(listenFd);

        int opt = 1;
        ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(owner_.cfg_.port);
        if (owner_.cfg_.bindAddress == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else if (::inet_pton(AF_INET, owner_.cfg_.bindAddress.c_str(), &addr.sin_addr) <= 0) {
            return false;
        }

        if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return false;
        if (!setNonBlocking(listenFd)) return false;
        if (::listen(listenFd, static_cast<int>(owner_.cfg_.maxClients)) < 0) return false;

        const int epfd = ::epoll_create1(EPOLL_CLOEXEC);
        if (epfd < 0) return false;
        epollFd_ = FdWrapper(epfd);

        const int efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (efd < 0) return false;
        wakeFd_ = FdWrapper(efd);

        listenFd_ = std::move(listenWrap);

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = kListenToken;
        ::epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, listenFd_.get(), &ev);

        epoll_event wev{};
        wev.events = EPOLLIN;
        wev.data.u64 = kWakeToken;
        ::epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, wakeFd_.get(), &wev);

        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() {
        running_.store(false);
        wake();
        if (listenFd_.get() >= 0) {
            ::shutdown(listenFd_.get(), SHUT_RDWR);
        }
    }

    void join() {
        if (thread_.joinable()) thread_.join();
    }

    void postResponse(uint64_t connId, Response resp) {
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            pending_.emplace_back(Pending{connId, std::move(resp)});
        }
        wake();
    }

private:
    struct Pending {
        uint64_t connId;
        Response resp;
    };

    struct Outgoing {
        enum class Kind : uint8_t { BYTES, FILE_FD, DMABUF };
        Kind kind{Kind::BYTES};
        std::string bytes;
        size_t bytesOffset{0};
        Body::FileFd file;
        uint64_t fileSent{0};
        Body::DmaBuf dmabuf;
        uint64_t dmabufSent{0};
    };

    struct Connection {
        ConnectionContext ctx;
        FdWrapper fd;
        std::string in;
        std::deque<Outgoing> out;
        std::chrono::steady_clock::time_point lastActive{std::chrono::steady_clock::now()};
        bool isHttp{false}; // protocol chosen by sniff
        bool closing{false};
        bool wantWrite{false};
    };

    static constexpr uint64_t kListenToken = 1;
    static constexpr uint64_t kWakeToken = 2;
    static constexpr uint64_t kConnTokenBase = 1000;

    void wake() {
        const uint64_t one = 1;
        const ssize_t rc = ::write(wakeFd_.get(), &one, sizeof(one));
        (void)rc;
    }

    void loop() {
        running_.store(true);
        std::vector<epoll_event> events;
        events.resize(64);

        while (running_.load()) {
            const int n = ::epoll_wait(epollFd_.get(), events.data(), static_cast<int>(events.size()), 1000);
            const auto now = std::chrono::steady_clock::now();

            if (n < 0) {
                if (errno == EINTR) continue;
            }

            for (int i = 0; i < n; ++i) {
                const auto token = events[i].data.u64;
                if (token == kListenToken) {
                    acceptAll();
                } else if (token == kWakeToken) {
                    drainWake();
                    drainPending();
                } else if (token >= kConnTokenBase) {
                    const uint64_t connId = token - kConnTokenBase;
                    const uint32_t ev = events[i].events;
                    if (ev & (EPOLLHUP | EPOLLERR)) {
                        closeConn(connId);
                        continue;
                    }
                    if (ev & EPOLLIN) onReadable(connId);
                    if (ev & EPOLLOUT) onWritable(connId);
                }
            }

            reapIdle(now);
        }

        // shutdown all
        std::vector<uint64_t> ids;
        ids.reserve(conns_.size());
        for (const auto& kv : conns_) ids.push_back(kv.first);
        for (auto id : ids) closeConn(id);
    }

    void acceptAll() {
        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            const int fd = ::accept(listenFd_.get(), reinterpret_cast<sockaddr*>(&client), &len);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }

            if (!setNonBlocking(fd)) {
                ::close(fd);
                continue;
            }
            setTcpKeepAliveOptions(fd, owner_.cfg_);

            char ip[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
            const uint16_t port = ntohs(client.sin_port);

            const uint64_t id = nextConnId_++;
            Connection c;
            c.ctx.id = id;
            c.ctx.peer.ip = ip;
            c.ctx.peer.port = port;
            c.fd = FdWrapper(fd);
            c.in.reserve(8192);

            conns_.emplace(id, std::move(c));

            epoll_event ev{};
            ev.events = EPOLLIN;
            ev.data.u64 = kConnTokenBase + id;
            ::epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, fd, &ev);
        }
    }

    void drainWake() {
        uint64_t v = 0;
        while (::read(wakeFd_.get(), &v, sizeof(v)) > 0) {}
    }

    void drainPending() {
        std::deque<Pending> local;
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            local.swap(pending_);
        }

        for (auto& p : local) {
            auto it = conns_.find(p.connId);
            if (it == conns_.end()) continue;
            enqueueResponse(it->second, std::move(p.resp));
        }
    }

    void enqueueResponse(Connection& c, Response resp) {
        if (!resp.head.empty()) {
            Outgoing o;
            o.kind = Outgoing::Kind::BYTES;
            o.bytes = std::move(resp.head);
            c.out.push_back(std::move(o));
        }

        if (resp.body.kind == Body::Kind::BYTES) {
            Outgoing o;
            o.kind = Outgoing::Kind::BYTES;
            o.bytes = std::move(resp.body.bytes);
            c.out.push_back(std::move(o));
        } else if (resp.body.kind == Body::Kind::FILE_FD) {
            Outgoing o;
            o.kind = Outgoing::Kind::FILE_FD;
            o.file = std::move(resp.body.file);
            c.out.push_back(std::move(o));
        } else if (resp.body.kind == Body::Kind::DMABUF) {
            Outgoing o;
            o.kind = Outgoing::Kind::DMABUF;
            o.dmabuf = std::move(resp.body.dmabuf);
            c.out.push_back(std::move(o));
        }

        if (resp.close) c.closing = true;
        enableWrite(c);
    }

    void enableWrite(Connection& c) {
        if (c.wantWrite) return;
        c.wantWrite = true;
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLOUT;
        ev.data.u64 = kConnTokenBase + c.ctx.id;
        ::epoll_ctl(epollFd_.get(), EPOLL_CTL_MOD, c.fd.get(), &ev);
    }

    void disableWrite(Connection& c) {
        if (!c.wantWrite) return;
        c.wantWrite = false;
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = kConnTokenBase + c.ctx.id;
        ::epoll_ctl(epollFd_.get(), EPOLL_CTL_MOD, c.fd.get(), &ev);
    }

    void onReadable(uint64_t id) {
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        Connection& c = it->second;

        char buf[4096];
        while (true) {
            const ssize_t n = ::recv(c.fd.get(), buf, sizeof(buf), 0);
            if (n > 0) {
                c.lastActive = std::chrono::steady_clock::now();
                c.in.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0) {
                closeConn(id);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeConn(id);
            return;
        }

        if (!c.isHttp && looksLikeHttp(c.in)) c.isHttp = true;

        if (c.isHttp) {
            processHttp(c);
        } else {
            processLine(c);
        }
    }

    void processLine(Connection& c) {
        // split by '\n', keep remainder
        size_t pos = 0;
        while (true) {
            const auto nl = c.in.find('\n', pos);
            if (nl == std::string::npos) break;
            std::string line = c.in.substr(0, nl);
            c.in.erase(0, nl + 1);
            pos = 0;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            LineRequest req;
            req.raw = line;
            const auto sp = line.find(' ');
            if (sp == std::string::npos) {
                req.command = line;
            } else {
                req.command = line.substr(0, sp);
                req.params = line.substr(sp + 1);
            }

            owner_.workers_->enqueue([this, ctx = c.ctx, req = std::move(req), connId = c.ctx.id]() mutable {
                Response resp = owner_.lineRouter_.dispatch(ctx, req);
                postResponse(connId, std::move(resp));
            });
        }
    }

    static bool tryParseHttp(std::string& buf, HttpRequest& out) {
        const auto headerEnd = buf.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return false;

        const std::string headerBlock = buf.substr(0, headerEnd);
        std::string remain = buf.substr(headerEnd + 4);

        std::istringstream iss(headerBlock);
        std::string requestLine;
        if (!std::getline(iss, requestLine)) return false;
        if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

        std::istringstream rl(requestLine);
        rl >> out.method >> out.target >> out.version;
        if (out.method.empty() || out.target.empty() || out.version.empty()) return false;

        out.headers.clear();
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string k = toLower(trim(line.substr(0, colon)));
            const std::string v = trim(line.substr(colon + 1));
            if (!k.empty()) out.headers[k] = v;
        }

        // body (Content-Length only)
        size_t need = 0;
        const auto it = out.headers.find("content-length");
        if (it != out.headers.end()) {
            need = static_cast<size_t>(std::strtoul(it->second.c_str(), nullptr, 10));
        }
        if (remain.size() < need) return false;

        out.body = remain.substr(0, need);
        buf = remain.substr(need);
        return true;
    }

    void processHttp(Connection& c) {
        HttpRequest req;
        while (tryParseHttp(c.in, req)) {
            owner_.workers_->enqueue([this, ctx = c.ctx, reqCopy = req, connId = c.ctx.id]() mutable {
                Response resp = owner_.httpRouter_.dispatch(ctx, reqCopy);
                postResponse(connId, std::move(resp));
            });
        }
    }

    void onWritable(uint64_t id) {
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        Connection& c = it->second;

        while (!c.out.empty()) {
            Outgoing& o = c.out.front();
            if (o.kind == Outgoing::Kind::BYTES) {
                const char* data = o.bytes.data() + o.bytesOffset;
                const size_t len = o.bytes.size() - o.bytesOffset;
                const ssize_t n = ::send(c.fd.get(), data, len, MSG_NOSIGNAL);
                if (n > 0) {
                    o.bytesOffset += static_cast<size_t>(n);
                    if (o.bytesOffset == o.bytes.size()) c.out.pop_front();
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                closeConn(id);
                return;
            }

            if (o.kind == Outgoing::Kind::FILE_FD) {
                off_t off = static_cast<off_t>(o.file.offset + o.fileSent);
                const size_t left = static_cast<size_t>(o.file.length - o.fileSent);
                if (left == 0) {
                    c.out.pop_front();
                    continue;
                }
                const ssize_t n = ::sendfile(c.fd.get(), o.file.fd.get(), &off, left);
                if (n > 0) {
                    o.fileSent += static_cast<uint64_t>(n);
                    if (o.fileSent == o.file.length) c.out.pop_front();
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;

                // fallback to copy (read+send) for non-sendfile capable fd
                char tmp[8192];
                const ssize_t r = ::pread(o.file.fd.get(), tmp, sizeof(tmp),
                                          static_cast<off_t>(o.file.offset + o.fileSent));
                if (r <= 0) {
                    closeConn(id);
                    return;
                }
                const ssize_t s = ::send(c.fd.get(), tmp, static_cast<size_t>(r), MSG_NOSIGNAL);
                if (s > 0) {
                    o.fileSent += static_cast<uint64_t>(s);
                    if (o.fileSent == o.file.length) c.out.pop_front();
                    continue;
                }
                break;
            }

            if (o.kind == Outgoing::Kind::DMABUF) {
#if UTILSCORE_NET_HAS_DMABUF
                if (!o.dmabuf.buf) {
                    c.out.pop_front();
                    continue;
                }
                if (o.dmabufSent >= o.dmabuf.length) {
                    c.out.pop_front();
                    continue;
                }
                // v1: map + send (copy). Later: add platform-specific fast paths.
                auto view = o.dmabuf.buf->scopedMap();
                const uint64_t off = o.dmabuf.offset + o.dmabufSent;
                const uint64_t left = o.dmabuf.length - o.dmabufSent;
                const size_t chunk = static_cast<size_t>(std::min<uint64_t>(left, 64 * 1024));
                const ssize_t n = ::send(c.fd.get(),
                                         reinterpret_cast<const char*>(view.get() + off),
                                         chunk,
                                         MSG_NOSIGNAL);
                if (n > 0) {
                    o.dmabufSent += static_cast<uint64_t>(n);
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                closeConn(id);
                return;
#else
                // DMABUF not available in this build environment.
                closeConn(id);
                return;
#endif
            }
        }

        if (c.out.empty()) {
            disableWrite(c);
            if (c.closing) {
                closeConn(id);
                return;
            }
        }
    }

    void reapIdle(const std::chrono::steady_clock::time_point& now) {
        if (owner_.cfg_.idleTimeoutSec <= 0) return;
        const auto timeout = std::chrono::seconds(owner_.cfg_.idleTimeoutSec);
        std::vector<uint64_t> dead;
        for (const auto& kv : conns_) {
            if (now - kv.second.lastActive > timeout) dead.push_back(kv.first);
        }
        for (auto id : dead) closeConn(id);
    }

    void closeConn(uint64_t id) {
        auto it = conns_.find(id);
        if (it == conns_.end()) return;
        ::epoll_ctl(epollFd_.get(), EPOLL_CTL_DEL, it->second.fd.get(), nullptr);
        ::shutdown(it->second.fd.get(), SHUT_RDWR);
        conns_.erase(it);
    }

    Server& owner_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    FdWrapper epollFd_{-1};
    FdWrapper wakeFd_{-1};
    FdWrapper listenFd_{-1};

    uint64_t nextConnId_{1};
    std::unordered_map<uint64_t, Connection> conns_;

    std::mutex outMutex_;
    std::deque<Pending> pending_;
};

Server::Server(ServerConfig cfg)
    : impl_(new Impl(*this))
    , cfg_(std::move(cfg))
{
    workers_ = std::make_unique<asyncThreadPool>(cfg_.workerThreadsMin, cfg_.workerThreadsMax, cfg_.workerQueueSize);
}

Server::~Server() {
    stop();
    join();
}

bool Server::start() {
    if (running_.exchange(true)) return false;
    if (!impl_->start()) {
        running_.store(false);
        return false;
    }
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    impl_->stop();
}

void Server::join() {
    impl_->join();
}

} // namespace net
} // namespace utils

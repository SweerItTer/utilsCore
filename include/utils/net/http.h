/*
 * @FilePath: /include/utils/net/http.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: Minimal HTTP/1.1 request/response types for utils::net
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "json.h"
#include "response.h"

namespace utils {
namespace net {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version; // "HTTP/1.1"
    std::unordered_map<std::string, std::string> headers;
    std::string body; // optional; v1 supports Content-Length only

    // 返回去掉 query string 的请求路径.
    std::string path() const;
    // 将 body 解析为 JSON 对象, 便于插件直接处理结构化 API 请求.
    bool parseJsonBody(JsonValue& outValue, std::string& error) const;
};

class HttpResponse {
public:
    static HttpResponse ok() { return HttpResponse(200, "OK"); }
    static HttpResponse notFound() { return HttpResponse(404, "Not Found"); }
    static HttpResponse badRequest() { return HttpResponse(400, "Bad Request"); }
    static HttpResponse methodNotAllowed() { return HttpResponse(405, "Method Not Allowed"); }
    static HttpResponse serverError() { return HttpResponse(500, "Internal Server Error"); }

    HttpResponse& contentType(std::string v) & {
        headers_["Content-Type"] = std::move(v);
        return *this;
    }
    HttpResponse&& contentType(std::string v) && { return std::move(contentType(std::move(v))); }

    HttpResponse& header(std::string k, std::string v) & {
        headers_[std::move(k)] = std::move(v);
        return *this;
    }
    HttpResponse&& header(std::string k, std::string v) && { return std::move(header(std::move(k), std::move(v))); }

    HttpResponse& keepAlive(bool v) & {
        keepAlive_ = v;
        return *this;
    }
    HttpResponse&& keepAlive(bool v) && { return std::move(keepAlive(v)); }

    HttpResponse& body(std::string bytes) & {
        body_ = Body::fromBytes(std::move(bytes));
        return *this;
    }
    HttpResponse&& body(std::string bytes) && { return std::move(body(std::move(bytes))); }

    // 将 JSON 序列化为 application/json 响应体.
    HttpResponse& json(const JsonValue& value) &;
    HttpResponse&& json(const JsonValue& value) && { return std::move(json(value)); }

    HttpResponse& bodyFromFileFd(FdWrapper fd, uint64_t offset, uint64_t length) & {
        body_ = Body::fromFileFd(std::move(fd), offset, length);
        return *this;
    }
    HttpResponse&& bodyFromFileFd(FdWrapper fd, uint64_t offset, uint64_t length) && {
        return std::move(bodyFromFileFd(std::move(fd), offset, length));
    }

    HttpResponse& bodyFromDmaBuf(DmaBufferPtr buf, uint64_t offset, uint64_t length) & {
        body_ = Body::fromDmaBuf(std::move(buf), offset, length);
        return *this;
    }
    HttpResponse&& bodyFromDmaBuf(DmaBufferPtr buf, uint64_t offset, uint64_t length) && {
        return std::move(bodyFromDmaBuf(std::move(buf), offset, length));
    }

    // Consumes internal body (may hold move-only fd).
    Response toResponse();

private:
    HttpResponse(int code, std::string reason) : status_(code), reason_(std::move(reason)) {}

    int status_{200};
    std::string reason_{"OK"};
    std::unordered_map<std::string, std::string> headers_;
    Body body_;
    bool keepAlive_{true};
};

} // namespace net
} // namespace utils

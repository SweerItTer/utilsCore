/*
 * @FilePath: /src/utils/net/http.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: HTTP response serialization
 */

#include "net/http.h"

#include <sstream>

namespace utils {
namespace net {

namespace {

std::string stripQueryString(const std::string& target) {
    const auto queryPos = target.find('?');
    return (queryPos == std::string::npos) ? target : target.substr(0, queryPos);
}

} // namespace

static std::string toHeaderLines(const std::unordered_map<std::string, std::string>& headers) {
    std::ostringstream oss;
    for (const auto& kv : headers) {
        oss << kv.first << ": " << kv.second << "\r\n";
    }
    return oss.str();
}

std::string HttpRequest::path() const {
    return stripQueryString(target);
}

bool HttpRequest::parseJsonBody(JsonValue& outValue, std::string& error) const {
    return JsonValue::parse(body, outValue, error);
}

HttpResponse& HttpResponse::json(const JsonValue& value) & {
    headers_["Content-Type"] = "application/json; charset=utf-8";
    body_ = Body::fromBytes(value.stringify());
    return *this;
}

Response HttpResponse::toResponse() {
    Response r;

    uint64_t contentLength = 0;
    if (body_.kind == Body::Kind::BYTES) contentLength = body_.bytes.size();
    if (body_.kind == Body::Kind::FILE_FD) contentLength = body_.file.length;
    if (body_.kind == Body::Kind::DMABUF) contentLength = body_.dmabuf.length;

    std::unordered_map<std::string, std::string> headers = headers_;
    headers.emplace("Server", "utilsCore-net");
    headers["Content-Length"] = std::to_string(contentLength);
    if (headers.find("Content-Type") == headers.end()) {
        headers["Content-Type"] = "application/octet-stream";
    }
    headers["Connection"] = keepAlive_ ? "keep-alive" : "close";

    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_ << " " << reason_ << "\r\n";
    oss << toHeaderLines(headers);
    oss << "\r\n";

    r.head = oss.str();
    r.body = std::move(body_);
    r.close = !keepAlive_;
    return r;
}

} // namespace net
} // namespace utils

/*
 * @FilePath: /utilsCore/include/utils/net/response.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: NET 响应体 - 可表达 bytes / file-fd / dmabuf 内容发送
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "fdWrapper.h"

class DmaBuffer;
using DmaBufferPtr = std::shared_ptr<DmaBuffer>;

namespace utils {
namespace net {

struct Body {
    enum class Kind : uint8_t {
        EMPTY,
        BYTES,
        FILE_FD,
        DMABUF
    };

    struct FileFd {
        FdWrapper fd{FdWrapper(-1)};
        uint64_t offset{0};
        uint64_t length{0}; // bytes to send
    };

    struct DmaBuf {
        DmaBufferPtr buf;
        uint64_t offset{0};
        uint64_t length{0};
    };

    Kind kind{Kind::EMPTY};
    std::string bytes;
    FileFd file;
    DmaBuf dmabuf;

    static Body empty() { return Body{}; }

    static Body fromBytes(std::string data) {
        Body b;
        b.kind = Kind::BYTES;
        b.bytes = std::move(data);
        return b;
    }

    static Body fromFileFd(FdWrapper fd, uint64_t offset, uint64_t length) {
        Body b;
        b.kind = Kind::FILE_FD;
        b.file.fd = std::move(fd);
        b.file.offset = offset;
        b.file.length = length;
        return b;
    }

    static Body fromDmaBuf(DmaBufferPtr buf, uint64_t offset, uint64_t length) {
        Body b;
        b.kind = Kind::DMABUF;
        b.dmabuf.buf = std::move(buf);
        b.dmabuf.offset = offset;
        b.dmabuf.length = length;
        return b;
    }
};

struct Response {
    // 在 body 前的内容 (e.g. HTTP headers).
    std::string head;

    // 实际内容, 可以是文件数据
    Body body;

    // 为真时响应后关闭连接
    bool close{false};

    static Response bytes(std::string data, bool closeConn = false) {
        Response r;
        r.body = Body::fromBytes(std::move(data));
        r.close = closeConn;
        return r;
    }
};

} // namespace net
} // namespace utils

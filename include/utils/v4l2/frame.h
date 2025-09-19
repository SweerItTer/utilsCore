/*
 * @FilePath: /EdgeVision/include/utils/v4l2/frame.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-05 01:09:32
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#ifndef FRAME_H
#define FRAME_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include "v4l2/v4l2Exception.h"
#include "sharedBufferState.h"

struct FrameMeta {
    uint64_t    frame_id = -1;      // 单调递增
    uint64_t    timestamp_ns = -1;  // CLOCK_MONOTONIC
    int         index = -1;         // buffer index
    // 原始尺寸(便于放大 box)
    uint32_t    w = 0;
    uint32_t    h = 0;
};

// 统一帧接口(MMAP&DMABUF)
class Frame {
public:
    using SharedBufferPtr = std::shared_ptr<SharedBufferState>;
    FrameMeta meta;
    // 强类型枚举
    enum class MemoryType { Unknown, MMAP, DMABUF };

    Frame() noexcept;
    ~Frame();
    Frame(SharedBufferPtr s);
    
    MemoryType type() const noexcept { return type_; }
    
    // 二次检查
    void* data() const;
    int dmabuf_fd() const;

    size_t size() const { return state_->length; }
    uint64_t timestamp() const { return meta.timestamp_ns; }
    SharedBufferPtr sharedState() const noexcept { return state_; }
    int index() const { return meta.index; }

    void setTimestamp(uint64_t ts) { meta.timestamp_ns = ts; }

    void setReleaseCallback(std::function<void(int)> bufReleasCallback) {
        bufReleasCallback_ = bufReleasCallback;
    }
private:
    std::function<void(int)> bufReleasCallback_;
    
    MemoryType type_;
    SharedBufferPtr state_;
};

#endif // !FRAME_H
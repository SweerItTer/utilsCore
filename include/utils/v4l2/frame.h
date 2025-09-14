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

    FrameMeta meta;
    // 强类型枚举
    enum class MemoryType { Unknown, MMAP, DMABUF };

    Frame() noexcept
    : type_(MemoryType::Unknown), state_(nullptr) {}
    
    Frame(std::shared_ptr<SharedBufferState> s)
        : state_(std::move(s)) {
            if (nullptr == state_) {
                meta.index = -1;
                type_ = MemoryType::Unknown;
                return;
            }
            
            BufferBacking b = state_->backing;
            if (BufferBacking::MMAP == b) {
                type_ = MemoryType::MMAP;
            } else if (BufferBacking::DMABUF_FD == b || BufferBacking::DMABUF_OBJ == b) {
                type_ = MemoryType::DMABUF;
            } else {
                type_ = MemoryType::Unknown;
            }
        }
    
    MemoryType type() const noexcept { return type_; }
    
    // 二次检查(优于结构体)
    void* data() const {
        if (type_ != MemoryType::MMAP) {
            fprintf(stderr, "Frame is not MMAP type");
            return nullptr;
        }
        if (false == state_->valid.load(std::memory_order_acquire))  {
            std::fprintf(stderr, "Frame is released\n");
            return nullptr;
        }
        return state_->start; 
    }
    int dmabuf_fd() const {         
        if (MemoryType::DMABUF != type_) {
            fprintf(stderr, "Frame is not MMAP type");
            return -1;
        }
        if (false == state_->valid.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "Frame is released\n");
            return -1;
        }

        // 支持两种后端
        if (BufferBacking::DMABUF_FD == state_->backing) {
            return state_->dmabuf_fd();
        } else if (BufferBacking::DMABUF_OBJ == state_->backing && state_->dmabuf_ptr) {
            return state_->dmabuf_ptr->fd();
        }
        return -1;
    }
    size_t size() const { return state_->length; }
    uint64_t timestamp() const { return meta.timestamp_ns; }
    int index() const { return meta.index; }
    
    void setTimestamp(uint64_t ts) { meta.timestamp_ns = ts; }
    std::shared_ptr<const SharedBufferState> sharedState() const noexcept { return state_; }

private:
    // 私有成员不允许外部修改(优于结构体)
    MemoryType type_;
    std::shared_ptr<SharedBufferState> state_;
};

#endif // !FRAME_H
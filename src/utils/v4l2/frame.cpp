/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-19 03:31:36
 * @FilePath: /EdgeVision/src/utils/v4l2/frame.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "v4l2/frame.h"

Frame::Frame() noexcept 
    : type_(MemoryType::Unknown), state_(nullptr) {
}    

Frame::~Frame()
{
    if (bufReleasCallback_ && meta.index >= 0 && true == state_->valid ) {
        bufReleasCallback_(meta.index); // 最后一个引用释放时, 自动返还
    }
}

Frame::Frame(SharedBufferPtr s)
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
    
// 二次检查(优于结构体)
void* Frame::data() const {
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
int Frame::dmabuf_fd() const {         
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

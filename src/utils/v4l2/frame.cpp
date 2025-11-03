/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-19 03:31:36
 * @FilePath: /EdgeVision/src/utils/v4l2/frame.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "v4l2/frame.h"

FixedSizePool Frame::s_pool_(sizeof(Frame), 1024);

Frame::Frame() noexcept 
    : type_(MemoryType::Unknown), state_(nullptr) {
}    

Frame::~Frame()
{
    if (bufReleasCallback_ && state_ != nullptr) {
        if ( meta.index >= 0 )
            bufReleasCallback_(meta.index); // 最后一个引用释放时, 自动返还
    }
}

Frame::Frame(SharedBufferPtr s)
    : state_(std::move(s)), mutiPlane_(false) {
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

Frame::Frame(std::vector<SharedBufferPtr> states) 
    : states_(std::move(states)), mutiPlane_(true) {
    if (states_.empty() || states_[0] == nullptr) {
        meta.index = -1;
        type_ = MemoryType::Unknown;
        state_ = nullptr;
        return;
    }

    state_ = states_[0]; // 仅保存第一个平面的状态用于查询类型等信息

    BufferBacking b = state_->backing;
    if (BufferBacking::MMAP == b) {
        type_ = MemoryType::MMAP;
    } else if (BufferBacking::DMABUF_FD == b || BufferBacking::DMABUF_OBJ == b) {
        type_ = MemoryType::DMABUF;
    } else {
        type_ = MemoryType::Unknown;
    }
}


void * Frame::operator new(std::size_t size) {
    // 通常size == sizeof(Frame)
    if (size != sizeof(Frame)) {
        // 防止派生类使用错误大小
        return ::operator new(size);
    }
    return s_pool_.allocate();
}

void Frame::operator delete(void *p) noexcept {
    if (nullptr == p) return;
    s_pool_.deallocate(p);
}

// 二次检查(优于结构体)
void* Frame::data(int planeIndex) const {
    if (type_ != MemoryType::MMAP) {
        fprintf(stderr, "Frame is not MMAP type");
        return nullptr;
    }
    // 单平面直接返回state_ (states_[0])
    if (!mutiPlane_) {
        if (false == state_->valid.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "Frame is released\n");
            return nullptr;
        }
        return state_->start; 
    }
    // 多平面检查索引
    if (planeIndex < 0 || planeIndex >= states_.size()) {
        fprintf(stderr, "Invalid plane index %d\n", planeIndex);
        return nullptr;
    }
    if (false == states_[planeIndex]->valid.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "Frame plane %d is released\n", planeIndex);
        return nullptr;
    }
    return states_[planeIndex]->start; 
}

int Frame::dmabuf_fd(int planeIndex) const {         
    if (MemoryType::DMABUF != type_) {
        fprintf(stderr, "Frame is not MMAP type");
        return -1;
    }
    // 单平面直接返回state_ (states_[0])
    if (!mutiPlane_) {
        if (false == state_->valid.load(std::memory_order_acquire)) {
            std::fprintf(stderr, "Frame is released\n");
            return -1;
        }
        return state_->dmabuf_fd();
    }
    // 多平面检查索引
    if (planeIndex < 0 || planeIndex >= states_.size()) {
        fprintf(stderr, "Invalid plane index %d\n", planeIndex);
        return -1;
    }
    if (false == states_[planeIndex]->valid.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "Frame plane %d is released\n", planeIndex);
        return -1;
    }
    return states_[planeIndex]->dmabuf_fd();
}

size_t Frame::size() const
{
    if (!mutiPlane_) {
        return state_->length;
    }
    size_t total_size = 0;
    for (const auto& s : states_) {
        total_size += s->length;
    }
    return total_size;
}

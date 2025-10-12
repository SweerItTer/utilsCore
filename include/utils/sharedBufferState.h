/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-13 05:50:44
 * @FilePath: /EdgeVision/include/utils/sharedBufferState.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef SHARED_BUFFER_STATE_H
#define SHARED_BUFFER_STATE_H

#include <cstddef>
#include <unistd.h>
#include <sys/mman.h>

#include "dma/dmaBuffer.h"

enum class BufferBacking {
    NONE,
    MMAP,        // 仅映射到用户态的指针，fd 归外部/驱动管理
    DMABUF_FD,   // 由本对象拥有的 dmabuf 裸 fd
    DMABUF_OBJ   // 由本对象拥有的 DmaBufferPtr (包含相关信息)
};

// 共享内存状态
struct SharedBufferState {
    // 所有权标志
    BufferBacking backing = BufferBacking::NONE;
    int           rawFd = -1;            // 当 backing == DMABUF_FD
    DmaBufferPtr  dmabuf_ptr = nullptr;  // 当 backing == DMABUF_OBJ
    void*         start = nullptr;       // 当 backing == MMAP
    size_t        length = 0;

    // 跨线程可读的有效标志
    std::atomic<bool> valid;
    // 裸指针裸fd的情况
    SharedBufferState(int fd = -1, void* ptr = nullptr, size_t len = 0)
    : rawFd(fd), start(ptr), length(len), valid(true) {
        if (nullptr != start && -1 == rawFd) backing = BufferBacking::MMAP;
        else if (0 < rawFd && nullptr == start) backing = BufferBacking::DMABUF_FD;
    }
    // 使用智能指针的dmabuf
    SharedBufferState(DmaBufferPtr dmabuf_ptr_ = nullptr, void* ptr = nullptr)
    : dmabuf_ptr(dmabuf_ptr_), start(ptr), length(dmabuf_ptr_->size()), valid(true) {
        if (nullptr != start && nullptr == dmabuf_ptr_) backing = BufferBacking::MMAP;
        else if (nullptr != dmabuf_ptr_ && nullptr == start) backing = BufferBacking::DMABUF_OBJ;
    }

    
    int dmabuf_fd(){
        if (nullptr != dmabuf_ptr){
            return dmabuf_ptr->fd();
        } else return rawFd;
    }        
    
    ~SharedBufferState() {
        // 将 valid 置 false
        valid.store(false, std::memory_order_release);

        // 仅释放指定资源
        switch (backing)
        {
        case BufferBacking::MMAP:
            if (nullptr != start && 0 < length) {
                munmap(start, length);
            }
            start = nullptr;
            break;

        case BufferBacking::DMABUF_FD:
            if (0 <= rawFd) {
                close(rawFd);
            }
            rawFd = -1;
            break;

        case BufferBacking::DMABUF_OBJ:
            if (dmabuf_ptr) {
                dmabuf_ptr.reset(); // 交给对象自身释放 fd
            }
            break;
        default:
            break;
        }
        
        backing = BufferBacking::NONE;
        length = 0;
    }
    
    // 禁止拷贝, 允许共享指针管理
    SharedBufferState(const SharedBufferState&) = delete;
    SharedBufferState& operator=(const SharedBufferState&) = delete;

    // 允许移动
    SharedBufferState(SharedBufferState&&) = default;
    SharedBufferState& operator=(SharedBufferState&&) = default;
private:
    SharedBufferState() : valid(false) {}
};

#endif

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

// 共享内存状态
struct SharedBufferState {
    int dmabuf_fd = -1;     // DMABUF fd
    DmaBufferPtr dmabuf_ptr = nullptr;
    void* start = nullptr;  // MMAP 指针
    size_t length = 0;      // 长度
    bool valid = true;      // 是否可用
    
    SharedBufferState(int fd = -1, void* ptr = nullptr, size_t len = 0)
    : dmabuf_fd(fd), start(ptr), length(len), valid(true) {}
    SharedBufferState(DmaBufferPtr dmabuf_ptr_ = nullptr, void* ptr = nullptr)
    : dmabuf_ptr(dmabuf_ptr_), start(ptr), length(dmabuf_ptr_->size()), valid(true) {}
    
    ~SharedBufferState() {
        if (start) {
            munmap(start, length);
            start = nullptr;
        }
        if (0 <= dmabuf_fd) {
            close(dmabuf_fd);
            dmabuf_fd = -1;
        }
        if (dmabuf_ptr)
        {
            dmabuf_ptr.reset();
        }
        valid = false;
    }
};

#endif

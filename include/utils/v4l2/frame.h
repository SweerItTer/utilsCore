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

// 统一帧接口(MMAP&DMABUF)
class Frame {
public:
    // 强类型枚举
    enum class MemoryType { MMAP, DMABUF };
    
    // 只引用不新分配内存
    Frame(void* data, size_t size, uint64_t timestamp, int index)
        : type_(MemoryType::MMAP), data_(data), size_(size), 
          timestamp_(timestamp), index_(index) {}
    
    Frame(int dmabuf_fd, size_t size, uint64_t timestamp, int index)
        : type_(MemoryType::DMABUF), dmabuf_fd_(dmabuf_fd), size_(size),
          timestamp_(timestamp), index_(index) {}
    
    MemoryType type() const { return type_; }
    // 二次检查(优于结构体)
    void* data() const { 
        if (type_ != MemoryType::MMAP) 
            throw std::runtime_error("Frame is not MMAP type");
        return data_; 
    }
    int dmabuf_fd() const { 
        if (type_ != MemoryType::DMABUF) 
            throw std::runtime_error("Frame is not DMABUF type");
        return dmabuf_fd_; 
    }
    size_t size() const { return size_; }
    uint64_t timestamp() const { return timestamp_; }
    int index() const { return index_; }

private:
    // 私有成员不允许外部修改(优于结构体)
    MemoryType type_;
    // 联合体充分利用内存资源
    union {
        void* data_ = nullptr;
        int dmabuf_fd_;
    };
    size_t size_;
    uint64_t timestamp_;
    int index_;
};

#endif // !FRAME_H
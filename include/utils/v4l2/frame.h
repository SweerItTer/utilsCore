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

struct SharedBufferState;

// 统一帧接口(MMAP&DMABUF)
class Frame {
public:
    Frame() = default;
    // 强类型枚举
    enum class MemoryType { MMAP, DMABUF };
    
    // 只引用不新分配内存
    Frame(std::shared_ptr<SharedBufferState> s
            , uint64_t timestamp, int index)
        : state_(std::move(s)), timestamp_(timestamp), index_(index) {
            if (nullptr == state_) {
                index_ = -1;
                return;
            }
            if (nullptr != state_->start) 
                type_ = MemoryType::MMAP;
            else type_ = MemoryType::DMABUF;
        }
    
    MemoryType type() const { return type_; }
    
    // 二次检查(优于结构体)
    void* data() const {
        if (type_ != MemoryType::MMAP) 
            throw V4L2Exception("Frame is not MMAP type");
        if (false == state_->valid)
            throw V4L2Exception("Frame is released");
        return state_->start; 
    }
    int dmabuf_fd() const { 
        if (type_ != MemoryType::DMABUF) 
            throw V4L2Exception("Frame is not DMABUF type");
        if (false == state_->valid){
            fprintf(stdout, "Frame is released");
            return -1;
        }
        return state_->dmabuf_fd;
    }
    size_t size() const { return state_->length; }
    uint64_t timestamp() const { return timestamp_; }
    int index() const { return index_; }
    
    void setTimestamp(uint64_t ts) { timestamp_ = ts; }
private:
    // 私有成员不允许外部修改(优于结构体)
    MemoryType type_;
    std::shared_ptr<SharedBufferState> state_;
    uint64_t timestamp_;
    int index_;
};

#endif // !FRAME_H
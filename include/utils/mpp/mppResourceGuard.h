/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-18 19:07:25
 * @FilePath: /EdgeVision/include/utils/mpp/mppResourceGuard.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>

// MppFrame RAII 守卫类
class MppFrameGuard {
public:
    explicit MppFrameGuard(MppFrame* f) : frame_(f) {}
    ~MppFrameGuard() { if (frame_ && *frame_) mpp_frame_deinit(frame_); }
    MppFrame operator->() const { return *frame_; }
    MppFrame get() const { return *frame_; }
    void release() { frame_ = nullptr; }
private:
    MppFrame* frame_;
};

// MppBuffer RAII 守卫类
class MppBufferGuard {
public:
    explicit MppBufferGuard(MppBuffer* b) : buffer_(b) {}
    ~MppBufferGuard() { if (buffer_ && *buffer_) mpp_buffer_put(*buffer_); }
    MppBuffer operator->() const { return *buffer_; }
    MppBuffer get() const { return *buffer_; }
    void release() { buffer_ = nullptr; }
private:
    MppBuffer* buffer_;
};

// MppPacket RAII 守卫类
class MppPacketGuard {
public:
    explicit MppPacketGuard(MppPacket* p) : packet_(p) {}
    ~MppPacketGuard() { if (packet_ && *packet_) mpp_packet_deinit(packet_); }
    void release() { packet_ = nullptr; }
private:
    MppPacket* packet_;
};
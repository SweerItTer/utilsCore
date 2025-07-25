/*
 * @FilePath: /EdgeVision/src/utils/rga/rgaProcessor.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:38
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "rga/rgaProcessor.h"

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <chrono>

RgaProcessor::RgaProcessor(std::shared_ptr<CameraController> cctr,
                        std::shared_ptr<FrameQueue> rawQueue,
                        std::shared_ptr<FrameQueue> outQueue,
                        int width, int height,
                        int srcFormat, int dstFormat, int poolSize)
    : rawQueue_(std::move(rawQueue)) // 原始帧队列
    , outQueue_(std::move(outQueue)) // 转换帧队列
    , cctr_(cctr)
    , width_(width)
    , height_(height)
    , srcFormat_(srcFormat)
    , dstFormat_(dstFormat)
    , poolSize_(poolSize)
    , running_(false)
{
    // 初始化固定缓冲池
    int buffer_size = width_ * height_ * 4;
    for (int i = 0; i < poolSize_; ++i) {
        void* data = malloc(buffer_size);
        if (data == nullptr) {
            std::cerr << "RGA: Failed to allocate buffer #" << i << std::endl;
            continue;
        }
        bufferPool_.emplace_back(RgbaBuffer{data, false});
    }
}


RgaProcessor::~RgaProcessor()
{
    stop();
    for (auto& buf : bufferPool_) {
        if (buf.data) {
            free(buf.data);
            buf.data = nullptr;
        }
    }
}

void RgaProcessor::start()
{
    if (running_) return;

    running_ = true;
    worker_ = std::thread(&RgaProcessor::run, this);
}

void RgaProcessor::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void RgaProcessor::releaseBuffer(int index)
{
    // fprintf(stdout, "Buffer released\n");
    // 在 size 范围内
    if (index >= 0 && index < static_cast<int>(bufferPool_.size())) {
        bufferPool_[index].in_use = false;
    }
}


void RgaProcessor::run()
{
    int buffer_size = width_ * height_ * 4;

    while (true == running_)
    {
        Frame frame(nullptr, 0, 0, -1);
        // 等待帧
        if (!rawQueue_->try_dequeue(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        // 查找可用缓冲区
        int retries = 0;
        int index = -1;
        for (int i = 0; i < poolSize_; ++i) {
            int try_idx = (currentIndex_ + i) % poolSize_;
            if (false == bufferPool_[try_idx].in_use && bufferPool_[try_idx].data) {
                bufferPool_[try_idx].in_use = true;
                index = try_idx;
                currentIndex_ = (try_idx + 1) % poolSize_;
                break;
            }
        }
        if (index == -1) {
            // 无可用 buffer
            std::cerr << "RGA: No free buffer, dropping frame.\n";
            // 丢帧
            cctr_->returnBuffer(frame.index());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        // 取出可用buf
        void* dst_data = bufferPool_[index].data;

        // 源图像
        rga_buffer_t src {};
        src.vir_addr = frame.data();
        src.width = width_;
        src.height = height_;
        src.wstride = width_;
        src.hstride = height_;
        src.format = srcFormat_;

        rga_buffer_t dst {};
        dst.vir_addr = dst_data;
        dst.width = width_;
        dst.height = height_;
        dst.wstride = width_;
        dst.hstride = height_;
        dst.format = dstFormat_;

        im_rect rect = {0, 0, width_, height_};
        RgaConverter::RgaParams params {src, rect, dst, rect};
        // 格式转换
        IM_STATUS status = (RK_FORMAT_YCbCr_420_SP == srcFormat_)
                         ? converter_.NV12toRGBA(params)
                         : converter_.NV16toRGBA(params);
        
        // rawQueue_ 需要 returnBuffer, 需要传递 CameraController
        // 不管是否转换成功都归还
        cctr_->returnBuffer(frame.index());
        
        if (IM_STATUS_SUCCESS != status) {
            std::cerr << "RGA convert failed\n";
            // 不释放内存，仅标记缓冲区可用
            bufferPool_[index].in_use = false;
            continue;
        }
        // fprintf(stdout, "RGA 转换完成.");
        
        // 构造新 Frame
        Frame result(dst_data, buffer_size, 0, index);
        outQueue_->enqueue(std::move(result));
    }
}

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
                           uint32_t width, uint32_t height,
                           Frame::MemoryType frameType,
                           int srcFormat, int dstFormat,
                           int poolSize)
    : rawQueue_(std::move(rawQueue))
    , outQueue_(std::move(outQueue))
    , cctr_(std::move(cctr))
    , width_(width)
    , height_(height)
    , frameType_(frameType)
    , srcFormat_(srcFormat)
    , dstFormat_(dstFormat)
    , poolSize_(poolSize)
    , running_(false)
{
    initpool();
}

void RgaProcessor::initpool(){
    int buffer_size = width_ * height_ * 4;

    for (int i = 0; i < poolSize_; ++i) {
        RgbaBuffer buf;

        if (Frame::MemoryType::MMAP == frameType_) {
            buf.data = malloc(buffer_size);
            if (nullptr == buf.data) {
                std::cerr << "RGA: malloc buffer failed at #" << i << std::endl;
                continue;
            }
        } else {
            uint32_t format = (RK_FORMAT_RGBA_8888 == dstFormat_)
                                ? DRM_FORMAT_RGBA8888
                                : DRM_FORMAT_XRGB8888;

            buf.dma_buf = DmaBuffer::create(width_, height_, format);
            if (!buf.dma_buf || buf.dma_buf->fd() < 0) {
                std::cerr << "RGA: dmabuf create failed at #" << i << std::endl;
                continue;
            }
        }

        bufferPool_.emplace_back(std::move(buf));
    }
}

RgaProcessor::~RgaProcessor()
{
    stop();
    std::vector<RgbaBuffer> temp = {};
    bufferPool_.swap(temp);
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

int RgaProcessor::getAvailableBufferIndex()
{
    for (int i = 0; i < poolSize_; ++i) {
        int try_idx = (currentIndex_ + i) % poolSize_;
        auto& buf = bufferPool_[try_idx];

        if (false == buf.in_use) {
            if ((Frame::MemoryType::MMAP == frameType_ && nullptr != buf.data) ||
                (Frame::MemoryType::DMABUF == frameType_ && buf.dma_buf && buf.dma_buf->fd() >= 0))
            {
                buf.in_use = true;
                currentIndex_ = (try_idx + 1) % poolSize_;
                return try_idx;
            }
        }
    }

    return -1; // 无可用缓冲
}


int RgaProcessor::dmabufFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, int dmabuf_fd)
{
    int index = getAvailableBufferIndex();
    if (0 > index){
        // 无可用buff
        return -1;
    }

    src.width = width_;
    src.height = height_;
    src.wstride = width_;
    src.hstride = height_;
    src.format = srcFormat_;
    
    dst = src;
    
    src.fd = dmabuf_fd;
    dst.fd = bufferPool_[index].dma_buf->fd();
    
    dst.format = dstFormat_;
    return index;
}

int RgaProcessor::mmapPtrFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, void* data)
{
    int index = getAvailableBufferIndex();
    if (0 > index){
        // 无可用buff
        return -1;
    }

    src.width = width_;
    src.height = height_;
    src.wstride = width_;
    src.hstride = height_;
    src.format = srcFormat_;
    
    dst = src;
    
    src.vir_addr = data;
    dst.vir_addr = bufferPool_[index].data;;
    
    dst.format = dstFormat_;
    return index;
}

void RgaProcessor::run()
{
    int buffer_size = width_ * height_ * 4;

    // 源图像
    rga_buffer_t src {};
    // 输出图像
    rga_buffer_t dst {};

    while (true == running_)
    {
        Frame frame(nullptr, 0, 0, -1);
        int index = 0;

        // 等待帧
        if (!rawQueue_->try_dequeue(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        switch (frameType_) {
            case Frame::MemoryType::MMAP:
                index = mmapPtrFrameProcess(src, dst, frame.data());
                break;
            case Frame::MemoryType::DMABUF:
                index = dmabufFrameProcess(src, dst, frame.dmabuf_fd());
                break;
            default:
                std::cerr << "Unsupported frame type.\n";
                cctr_->returnBuffer(frame.index());
                continue;
        }
        
        if (0 > index) {
            // 无可用 buffer
            // std::cerr << "RGA: No free buffer, dropping frame.\n";
            // 丢帧
            cctr_->returnBuffer(frame.index());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        im_rect rect = {0, 0, static_cast<int>(width_), static_cast<int>(height_)};
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
        

        // 构造新 Frame
        Frame result = (Frame::MemoryType::MMAP == frameType_)
            ? Frame(bufferPool_[index].data, buffer_size, 0, index)
            : Frame(bufferPool_[index].dma_buf->fd(), buffer_size, 0, index);
        outQueue_->enqueue(std::move(result));
    }
}

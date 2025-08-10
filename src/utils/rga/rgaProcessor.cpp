/*
 * @FilePath: /EdgeVision/src/utils/rga/rgaProcessor.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:38
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "rga/rgaProcessor.h"
#include "logger.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <chrono>

RgaProcessor::RgaProcessor(Config& cfg)
    : cctr_(std::move(cfg.cctr))
    , rawQueue_(std::move(cfg.rawQueue))
    , outQueue_(std::move(cfg.outQueue))
    , width_(cfg.width)
    , height_(cfg.height)
    , frameType_(cfg.frameType)
    , srcFormat_(cfg.srcFormat)
    , dstFormat_(cfg.dstFormat)
    , poolSize_(cfg.poolSize)
    , running_(false), paused(false)
{
    initpool();
}

void RgaProcessor::initpool() {
    for (int i = 0; i < poolSize_; ++i) {
        RgbaBuffer buf;

        if (Frame::MemoryType::MMAP == frameType_) {
            int buffer_size = width_ * height_ * 4;
            buf.data = malloc(buffer_size);
            if (nullptr == buf.data) {
                throw std::runtime_error("RGA: malloc buffer failed");
            }
        } else {
            uint32_t format = (RK_FORMAT_RGBA_8888 == dstFormat_)
                                ? DRM_FORMAT_RGBA8888
                                : DRM_FORMAT_XRGB8888;
            buf.dma_buf = DmaBuffer::create(width_, height_, format);
            if (!buf.dma_buf || buf.dma_buf->fd() < 0) {
                throw std::runtime_error("RGA: dmabuf create failed");
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
    if (true == paused) {
		paused = false;
	}

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

    auto size = rawQueue_->size();
    for (int i=0; i < size; i++){
        cctr_->returnBuffer(rawQueue_->dequeue().index());
    }

    outQueue_->clear();
}

void RgaProcessor::pause(){
	paused = true;
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
        if ( true == paused ) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if ( false == running_ ) break;
        }
        Frame frame(nullptr, 0, 0, -1);
        int index = 0;

        // 等待帧
        if (!rawQueue_->try_dequeue(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        // 计算 RGA_submit timestamp
        uint64_t t1;
        mk::makeTimestamp(t1);
        Logger::log(stdout, "[DQ→RGA_submit] = %llu", t1-frame.timestamp());
        
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
        // 同步回调时间点
        uint64_t t2;
        mk::makeTimestamp(t2);
        Logger::log(stdout, "[RGA_process] = %llu", t2-t1);
        // rawQueue_ 需要 returnBuffer, 需要传递 CameraController
        // 不管是否转换成功都归还
        cctr_->returnBuffer(frame.index());
        
        if (IM_STATUS_SUCCESS != status) {
            fprintf(stderr, "RGA convert failed: %d\n", status);
            // 不释放内存，仅标记缓冲区可用
            bufferPool_[index].in_use = false;
            continue;
        }
        

        // 构造新 Frame
        Frame result = (Frame::MemoryType::MMAP == frameType_)
            ? Frame(bufferPool_[index].data, buffer_size, t2, index)
            : Frame(bufferPool_[index].dma_buf->fd(), buffer_size, t2, index);
        outQueue_->enqueue(std::move(result));
        // static int a = 1;
        // if (1 == a){
        //     a = 0;
        //     auto temp = bufferPool_[index].dma_buf;
        //     dumpDmabufAsRGBA(temp->fd(), temp->width(), temp->height(), temp->size(), temp->pitch(), "/end.rgba");
        // }
    }
}

bool RgaProcessor::dumpDmabufAsRGBA(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t size, uint32_t pitch, const char* path)
{
    if (dmabuf_fd < 0 || width == 0 || height == 0 || size == 0 || pitch == 0) {
        fprintf(stderr, "[dump] Invalid argument\n");
        return false;
    }

    void* data = mmap(nullptr, size, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
    if (MAP_FAILED == data) {
        perror("mmap failed");
        return false;
    }

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        perror("fopen failed");
        munmap(data, size);
        return false;
    }

    // 写入原始 RGBA 图像数据（每像素 4 字节，含 Alpha）
    uint8_t* ptr = static_cast<uint8_t*>(data);
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = ptr + y * pitch;
        fwrite(row, 1, width * 4, fp);  // 每行写 width × 4 字节
    }

    fclose(fp);
    munmap(data, size);

    fprintf(stderr, "[dump] Saved %dx%d RGBA8888 raw image to %s\n", width, height, path);
    return true;
}
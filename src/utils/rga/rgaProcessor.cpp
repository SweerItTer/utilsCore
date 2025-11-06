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
#include <sys/syscall.h>

#include "threadUtils.h"

RgaProcessor::RgaProcessor(Config& cfg)
    : cctr_(std::move(cfg.cctr))
    , rawQueue_(std::move(cfg.rawQueue))
    , width_(cfg.width)
    , height_(cfg.height)
    , srcFormat_(cfg.srcFormat)
    , dstFormat_(cfg.dstFormat)
    , poolSize_(cfg.poolSize)
    , running_(false), paused(false)
{
    frameType_ = (true == cfg.usingDMABUF)
            ? Frame::MemoryType::DMABUF
            : Frame::MemoryType::MMAP;
    initpool();
    rgaThreadPool = std::make_unique<asyncThreadPool>(poolSize_);
}

void RgaProcessor::initpool() {
    for (int i = 0; i < poolSize_; ++i) {
        RgbaBuffer buf;

        if (Frame::MemoryType::MMAP == frameType_) {
            int buffer_size = width_ * height_ * 4;
            // 使用 mmap 替代 malloc 以适配 munmap 释放
            void* data = mmap(nullptr, buffer_size, 
                             PROT_READ | PROT_WRITE,
                             MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (data == MAP_FAILED) {
                throw std::runtime_error("RGA: mmap buffer failed");
            }
            buf.s = std::make_shared<SharedBufferState>(-1, data, buffer_size);
        } else {
            uint32_t format = formatRGAtoDRM(dstFormat_);
            // 实际格式是DRM的
            auto dma_buf = DmaBuffer::create(width_, height_, format, 0);
            if (!dma_buf || dma_buf->fd() < 0) {
                throw std::runtime_error("RGA: dmabuf create failed");
            }
            buf.s = std::make_shared<SharedBufferState>(dma_buf, nullptr);
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
}

void RgaProcessor::pause(){
	paused = true;
}

void RgaProcessor::releaseBuffer(int index)
{
    // 在 size 范围内
    if (index >= 0 && index < static_cast<int>(bufferPool_.size())) {
        bufferPool_[index].in_use = false;
        // fprintf(stdout, "Buffer %d released\n", bufferPool_[index].s->dmabuf_ptr->fd());
    }
}

int RgaProcessor::getAvailableBufferIndex()
{
    for (int i = 0; i < poolSize_; ++i) {
        int try_idx = (currentIndex_ + i) % poolSize_;
        auto& buf = bufferPool_[try_idx];

        // 检查缓冲区是否可用
        if (true == buf.in_use || nullptr == buf.s || false == buf.s->valid) {
            // fprintf(stdout, "bufferPool_ using\n");
            continue;
        }
        if (Frame::MemoryType::DMABUF == frameType_) {
            // 确保 DMABUF 有效
            if (buf.s->dmabuf_ptr && 0 <= buf.s->dmabuf_ptr->fd()) {
                buf.in_use = true;
                currentIndex_ = (try_idx + 1) % poolSize_;
                return try_idx;
            }
        } else if (Frame::MemoryType::MMAP == frameType_){
            // 确保虚拟内存有效
            if (buf.s->start) {
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
    // 数据无效
    if (0 > dmabuf_fd) return -1;

    src.width = width_;
    src.height = height_;
    src.wstride = width_;
    src.hstride = height_;
    src.format = srcFormat_;
    
    dst = src;
    
    src.fd = dmabuf_fd;
    dst.fd = bufferPool_[index].s->dmabuf_ptr->fd();
    
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
    // 数据无效
    if (nullptr == data) return -1;

    src.width = width_;
    src.height = height_;
    src.wstride = width_;
    src.hstride = height_;
    src.format = srcFormat_;
    
    dst = src;
    
    src.vir_addr = data;
    dst.vir_addr = bufferPool_[index].s->start;
    
    dst.format = dstFormat_;
    return index;
}

int RgaProcessor::getIndex_auto(rga_buffer_t& src, rga_buffer_t& dst, Frame* frame){
    int index = -1;
    switch (frameType_) {
        case Frame::MemoryType::MMAP:
            index = mmapPtrFrameProcess(src, dst, frame->data());
            break;
        case Frame::MemoryType::DMABUF:
            index = dmabufFrameProcess(src, dst, frame->dmabuf_fd(0));
            break;
        default:
            std::cerr << "Unsupported frame type.\n";
            break;
    }
    return index;
}

void RgaProcessor::setThreadAffinity(int cpu_core) {
    // 在内部处理线程中设置亲和性
    if(worker_.joinable()) {
        ThreadUtils::safeBindThread(worker_, cpu_core);
    }
}

// 实际处理函数
FramePtr RgaProcessor::infer() {
    rga_buffer_t src {};// 源图像参数
    rga_buffer_t dst {};// 输出图像参数
    FramePtr rawFrame = nullptr;
    FramePtr dstFrame = nullptr;
    int retry = 3;
    // 等待帧
    do {
        if (retry--) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        return dstFrame;
    } while (!rawQueue_->try_dequeue(rawFrame));
    if (!running_) return dstFrame;
    int index = getIndex_auto(src, dst, rawFrame.get());

    if (0 > index) {
        // 无可用 buffer(或格式错误)
        // fprintf(stdout,"RGA: No free buffer, dropping rawFrame.\n");
        // 丢帧
        rawFrame.reset();
        return dstFrame;
    }
    // 若提前释放内存需撤销占用
    auto state = rawFrame->sharedState(0);
    if (nullptr == state) {
        fprintf(stderr, "[RGAProcesse] Get null frame.\n");
        bufferPool_[index].in_use = false;
        return dstFrame;
    }
    if (false == state->valid){
        fprintf(stderr, "[RGAProcesse] Frame is invaild.\n");
        bufferPool_[index].in_use = false;
        return dstFrame;
    }
    
    im_rect rect = {0, 0, static_cast<int>(width_), static_cast<int>(height_)};
    RgaConverter::RgaParams params {src, rect, dst, rect};
    // 格式转换
    IM_STATUS status = RgaConverter::instance().FormatTransform(params);

    // 复制元数据
    auto rgaMeta = rawFrame->meta;
    rgaMeta.index = index;
    
    // 不管是否转换成功都归还
    rawFrame.reset();
    // cctr_->returnBuffer(rawFrame->index());
    
    if (IM_STATUS_SUCCESS != status) {
        fprintf(stderr, "RGA convert failed: %d\n", status);
        // 不释放内存, 仅标记缓冲区可用
        bufferPool_[index].in_use = false;
        return dstFrame;
    }

    // 直接使用池中的共享指针
    dstFrame.reset( new Frame(bufferPool_[index].s) );
    dstFrame->meta = rgaMeta;
    dstFrame->setReleaseCallback([this](int index){
        releaseBuffer(index);
    });
    
    return std::move(dstFrame);
}

void RgaProcessor::run()
{
    std::cout << "RGA main thread TID: " << syscall(SYS_gettid) << "\n";
    int buffer_size = width_ * height_ * 4;

    while (true == running_)
    {
        if ( true == paused ) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (false == running_) break;
            continue;
        }
        // 添加异步任务
        auto fut = rgaThreadPool->enqueue([this](){ return infer(); });
        // 只有当 future 有效时才加入
        if (fut.valid()) {
            futs.emplace_back(std::move(fut));
        }
    }
}

// 取出future
int RgaProcessor::dump(FramePtr& frame, int64_t timeout) {
    frame = nullptr;
    if (futs.empty()) return -1;

    auto& f = futs.front();
    // 非阻塞尝试等待任务完成
    if (f.wait_for(std::chrono::milliseconds(timeout)) != std::future_status::ready) {
        return -1;
    }

    try { frame = f.get(); } // 获取任务结果
    catch (const std::exception& e) { frame = nullptr; }
    futs.pop_front();
    return (frame == nullptr ? -1 : 0);
}

bool RgaProcessor::dumpDmabufAsXXXX8888(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t size, uint32_t pitch, const char* path)
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

    // 写入原始图像数据 每像素 4 字节, 4字节 形如 [R,G,B,A]
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
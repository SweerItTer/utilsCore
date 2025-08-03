/*
 * @FilePath: /EdgeVision/include/utils/rga/rgaProcessor.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:16
 * @LastEditors: Please set LastEditors
 */
#ifndef RGAPROCESSTHREAD_H
#define RGAPROCESSTHREAD_H

#include <atomic>
#include <thread>
#include <memory>

#include "types.h"
#include "v4l2/cameraController.h"
#include "rga/rgaConverter.h"
#include "dma/dmaBuffer.h"


struct RgbaBuffer {
    void* data = nullptr;
    DmaBufferPtr dma_buf = nullptr;
    bool in_use = false;

    RgbaBuffer() = default;

    // 禁用拷贝构造和赋值
    RgbaBuffer(const RgbaBuffer&) = delete;
    RgbaBuffer& operator=(const RgbaBuffer&) = delete;

    // 移动构造函数
    RgbaBuffer(RgbaBuffer&& other) noexcept
        : data(other.data),
          dma_buf(std::move(other.dma_buf)),
          in_use(other.in_use)
    {
        other.data = nullptr;  // 转移后置空，避免析构时重复 free
        other.in_use = false;
    }

    // 移动赋值操作符
    RgbaBuffer& operator=(RgbaBuffer&& other) noexcept {
        if (this != &other) {
            // 释放当前对象已有资源
            if (nullptr != data) {
                free(data);
            }
            data = other.data;
            dma_buf = std::move(other.dma_buf);
            in_use = other.in_use;

            // 转移后置空
            other.data = nullptr;
            other.in_use = false;
        }
        return *this;
    }

    // 析构函数
    ~RgbaBuffer() {
        if (nullptr != data) {
            free(data);
            data = nullptr;
        }
    }
};

class RgaProcessor {
public:
    struct Config
    {
        std::shared_ptr<CameraController> cctr = nullptr;
        std::shared_ptr<FrameQueue> rawQueue = nullptr;
        std::shared_ptr<FrameQueue> outQueue = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        Frame::MemoryType frameType = Frame::MemoryType::MMAP;
        int dstFormat = RK_FORMAT_RGBA_8888;
        int srcFormat = RK_FORMAT_YCbCr_420_SP;
        int poolSize = 4;
    };
     
    RgaProcessor(Config& cfg);

    ~RgaProcessor();

    void start();
    void stop();

    void pause();

    void releaseBuffer(int index);

    static bool dumpDmabufAsRGBA(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t size, uint32_t pitch, const char* path);

private:
    void initpool();
    int getAvailableBufferIndex();
    int dmabufFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, int dmabuf_fd);
    int mmapPtrFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, void* data);

private:
    void run();

    std::atomic_bool running_;
    std::atomic_bool paused;
    std::thread worker_;

    std::shared_ptr<FrameQueue> rawQueue_;
    std::shared_ptr<FrameQueue> outQueue_;
    std::shared_ptr<CameraController> cctr_;
    RgaConverter converter_;
    
    uint32_t width_;
    uint32_t height_;
    int dstFormat_;
    int srcFormat_;

    std::vector<RgbaBuffer> bufferPool_;
    int currentIndex_ = 0;
    const int poolSize_ = 0;
    const Frame::MemoryType frameType_;
};
    

#endif // RGAPROCESSTHREAD_H

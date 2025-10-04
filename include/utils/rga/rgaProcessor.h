/*
 * @FilePath: /EdgeVision/include/utils/rga/rgaProcessor.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:16
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
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
#include "rga/rga2drm.h"

struct RgbaBuffer {
    std::shared_ptr<SharedBufferState> s;
    bool in_use = false;
    // 析构函数
    ~RgbaBuffer() {
        if (nullptr != s) {
            s->valid = false;
            s.reset();
        }
    }

    RgbaBuffer() = default;

    // 禁用拷贝构造和赋值
    RgbaBuffer(const RgbaBuffer&) = delete;
    RgbaBuffer& operator=(const RgbaBuffer&) = delete;

    // 移动构造函数
    RgbaBuffer(RgbaBuffer&& other) noexcept
        : s(std::move(other.s))  // 转移所有权
        , in_use(other.in_use)   // 复制状态
    {
        // 将被移动对象置于安全状态
        other.in_use = false;  // 标记为不再使用
        // s 已自动置空，无需额外操作
    }

    // 移动赋值操作符
    RgbaBuffer& operator=(RgbaBuffer&& other) noexcept {
        if (this != &other) {
            // 转移资源所有权
            s = std::move(other.s);
            in_use = other.in_use;
            
            // 重置被移动对象状态
            other.in_use = false;  // 标记为不再使用
            // s 已自动置空，无需额外操作
        }
        return *this;
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
        bool usingDMABUF = false;
        int dstFormat = RK_FORMAT_RGBA_8888;
        int srcFormat = RK_FORMAT_YCbCr_420_SP;
        int poolSize = 4;
    };
     
    RgaProcessor(Config& cfg);

    ~RgaProcessor();

    void setYoloInputSize(int w, int h);
    void setThreadAffinity(int cpu_core);
    void start();
    void stop();
    void pause();

    void releaseBuffer(int index);

    static bool dumpDmabufAsXXXX8888(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t size, uint32_t pitch, const char* path);
private:
    void initpool();
    int getAvailableBufferIndex();
    int dmabufFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, int dmabuf_fd);
    int mmapPtrFrameProcess(rga_buffer_t& src, rga_buffer_t& dst, void* data);
    int getIndex_auto(rga_buffer_t& src, rga_buffer_t& dst, Frame* frame);
private:
    void run();

    std::atomic_bool running_;
    std::atomic_bool paused;
    std::thread worker_;

    std::shared_ptr<FrameQueue> rawQueue_;
    std::shared_ptr<FrameQueue> outQueue_;
    std::shared_ptr<CameraController> cctr_;
    RgaConverter* converter_;
    
    uint32_t width_;
    uint32_t height_;
    int dstFormat_;
    int srcFormat_;

    std::vector<RgbaBuffer> bufferPool_;
    int currentIndex_ = 0;
    const int poolSize_ = 0;
    Frame::MemoryType frameType_;

    int yoloW = 0;
    int yoloH = 0;
};
    

#endif // RGAPROCESSTHREAD_H

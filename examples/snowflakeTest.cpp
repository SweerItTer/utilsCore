/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-09 17:41:22
 * @FilePath: /EdgeVision/examples/SnowflakeTest.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "displayManager.h"
#include "rga/formatTool.h"

#include <vector>
#include <queue>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <random>
#include <thread>
#include <chrono>
#include <mutex>

static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running.store(false);
    }
}

// Buffer 包装器, 带状态标识
struct BufferSlot {
    DmaBufferPtr yPlane;
    DmaBufferPtr uvPlane;
    bool inUse;  // 标识是否已出队使用中
    
    BufferSlot() : inUse(false) {}
};


static void fillRandomNoiseNV12(DmaBufferPtr buf) {
    if (nullptr == buf) {
        std::cerr << "[Error] fillRandomNoiseNV12: buf is null" << std::endl;
        return;
    }

    auto pitch = buf->pitch();
    auto height = buf->height();

    size_t ySize = static_cast<size_t>(pitch) * static_cast<size_t>(height);
    size_t uvSize = ySize / 2;
    size_t total = ySize + uvSize;

    void* mapped = buf->map();
    if (nullptr == mapped) {
        std::cerr << "[Error] Failed to mmap buffer" << std::endl;
        return;
    }

    uint32_t* p = static_cast<uint32_t*>(mapped);
    size_t words = total / 4;

    // 每帧只更新一次 seed, 非常快
    static uint32_t seed = 0x12345678;
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);

    uint32_t s = seed;

    // 快速填充, 连续生成伪随机序列, 视觉效果像噪声
    for (size_t i = 0; i < words; ++i) {
        // 最快的“变化”: 加 0x9e3779b9 (黄金分割质数)
        s += 0x9e3779b9;
        p[i] = s;
    }

    // 若存在 tail
    uint8_t* tail = reinterpret_cast<uint8_t*>(p + words);
    for (size_t i = 0; i < total % 4; ++i) {
        tail[i] = static_cast<uint8_t>(s >> (i * 8));
    }

    // 不 unmap, 遵从你的规则
}

// 选择最接近屏幕分辨率的标准分辨率
static auto chooseClosestResolution(int screenW, int screenH) -> std::pair<int, int> {
    static const std::vector<std::pair<int, int>> standardRes = {
        {640, 480}, {720, 480}, {720, 576}, {1280, 720},
        {1920, 1080}, {2560, 1440}, {3840, 2160}, {4096, 2160}
    };

    std::pair<int, int> bestRes;
    int minDist = std::numeric_limits<int>::max();

    for (const auto& res : standardRes) {
        int dw = res.first - screenW;
        int dh = res.second - screenH;
        int dist = dw * dw + dh * dh;

        if (dist < minDist) {
            minDist = dist;
            bestRes = res;
        }
    }

    // 对齐 NV12
    int wAligned = (bestRes.first + 3) & ~3;
    int hAligned = (bestRes.second + 1) & ~1;

    return std::pair<int, int>(wAligned, hAligned);
}

int main() {
    DrmDev::fd_ptr = DeviceController::create();

    std::signal(SIGINT, handleSignal);

    // 创建 3 个 buffer 的循环队列
    constexpr int NUM_BUFFERS = 10;
    std::vector<BufferSlot> bufferPool;
    std::queue<int> availableQueue;  // 存储可用 buffer 的索引
    std::mutex queueMutex;

    DisplayManager dm;
    
    DisplayManager::PlaneHandle overlayPlaneHandle;
    std::atomic<uint32_t> autoWidth, autoHeight;
    std::atomic<bool> refreshing{false};

    auto post = [&]{
        auto screenSize = dm.getCurrentScreenSize();
        auto resolution = chooseClosestResolution(screenSize.first, screenSize.second);
        autoWidth  = resolution.first;
        autoHeight = resolution.second;
        
        DisplayManager::PlaneConfig overlayCfg {
            .type = DisplayManager::PlaneType::OVERLAY,
            .srcWidth = autoWidth,
            .srcHeight = autoHeight,
            .drmFormat = convertV4L2ToDrmFormat(V4L2_PIX_FMT_NV12),
            .zOrder = 1
        };
        overlayPlaneHandle = dm.createPlane(overlayCfg);
        std::cout << "[Main] OverlayPlane valid: " << overlayPlaneHandle.valid() << std::endl;
        std::cout << "[Main] Resolution: " << autoWidth << "x" << autoHeight << std::endl;

        // 初始化 buffer pool
        bufferPool.resize(NUM_BUFFERS);
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            auto Y = DmaBuffer::create(autoWidth, autoHeight, 
                                    convertV4L2ToDrmFormat(V4L2_PIX_FMT_NV12), 0, 0);
            auto UV = DmaBuffer::importFromFD(
                Y->fd(),
                Y->width(),
                Y->height() / 2,
                Y->format(),
                Y->pitch() * Y->height() / 2,
                Y->pitch() * Y->height()
            );
            
            bufferPool[i].yPlane = std::move(Y);
            bufferPool[i].uvPlane = std::move(UV);
            bufferPool[i].inUse = false;
            
            availableQueue.push(i);
            std::cout << "[Init] Created buffer slot " << i << std::endl;
        }
        refreshing = false;
    };

    dm.registerPreRefreshCallback([&]{
        refreshing = true;
        
        // 先停止主循环使用 buffer
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // 加锁清空所有资源
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // 清空队列
        while(!availableQueue.empty()) {
            availableQueue.pop();
        }
        
        // 清空 buffer pool
        bufferPool.clear();
    });
    
    dm.registerPostRefreshCallback(post);
    post();
    dm.start();

    int frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    int fpsCounter = 0;
    double currentFps = 0.0;
    
    while(running) {
        if (refreshing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running) break;
            continue;
        }
        
        int bufferIdx = -1;
        DmaBufferPtr yPlane;
        DmaBufferPtr uvPlane;
        
        // 从队列取出可用 buffer, 并复制 shared_ptr
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!availableQueue.empty() && !refreshing) {
                bufferIdx = availableQueue.front();
                availableQueue.pop();
                
                // 在锁内复制 shared_ptr, 增加引用计数
                if (bufferIdx < bufferPool.size()) {
                    yPlane = bufferPool[bufferIdx].yPlane;
                    uvPlane = bufferPool[bufferIdx].uvPlane;
                    bufferPool[bufferIdx].inUse = true;
                } else {
                    bufferIdx = -1;
                }
            }
        }
        
        if (bufferIdx == -1 || !yPlane || !uvPlane) {
            if (!refreshing) {
                std::cerr << "[Warning] No available buffer, skipping frame" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        
        // 填充随机数据
        fillRandomNoiseNV12(yPlane);
        
        // 提交显示
        if (overlayPlaneHandle.valid()) {
            std::vector<DmaBufferPtr> buffers;
            buffers.push_back(yPlane);
            buffers.push_back(uvPlane);
            
            dm.presentFrame(overlayPlaneHandle, buffers, nullptr);
            
            frameCount++;
            fpsCounter++;
        }
        
        // 归还 buffer 到队列
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (bufferIdx < bufferPool.size() && !refreshing) {
                bufferPool[bufferIdx].inUse = false;
                availableQueue.push(bufferIdx);
            }
        }
        
        // 计算并显示帧率(每秒更新一次)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime).count();
        if (elapsed >= 1000) {
            currentFps = fpsCounter * 1000.0 / elapsed;
            std::cout << "\r[FPS] " << std::fixed << std::setprecision(2) 
                      << currentFps << " fps | Total frames: " << frameCount 
                      << " | Buffer slot: " << bufferIdx << std::flush;
            fpsCounter = 0;
            lastFpsTime = now;
        }
    }
    
    dm.stop();
    std::cout << "[Main] Program Exit. Total frames: " << frameCount << std::endl;
    return 0;
}
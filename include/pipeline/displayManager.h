/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:57:40
 * @FilePath: /EdgeVision/include/pipeline/displayManager.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <memory>
#include <iostream>

#include "dma/dmaBuffer.h"			// dmabuf 管理

class DisplayManager {
public:
    class PlaneHandle {
    public:
        PlaneHandle(int id = -1) : id_(id) {}
        ~PlaneHandle() { release(); }

        PlaneHandle(const PlaneHandle& other);
        PlaneHandle& operator=(const PlaneHandle& other);
        PlaneHandle(PlaneHandle&& other) noexcept;
        PlaneHandle& operator=(PlaneHandle&& other) noexcept;
        
        // ---------- 常用接口 ----------
        bool valid() const { return id_.load(std::memory_order_acquire) >= 0; }

        void release() { 
            id_.store(-1, std::memory_order_release);
        }

        void reset(int id = -1) {
            id_.store(id, std::memory_order_release);
        }

        int get() const {
            return id_.load(std::memory_order_acquire);
        }
        
    private:
        std::atomic<int> id_;
    };

    enum class PlaneType {
        INVALID = -1,
        OVERLAY = 0,
        PRIMARY,
    // TODO: CURSOR
    };

    struct PlaneConfig {
        PlaneType type{PlaneType::INVALID};
        uint32_t  srcWidth{0};
        uint32_t  srcHeight{0};
        uint32_t  drmFormat{0};
        uint32_t  zOrder{0};
    };
    using RefreshCallback = std::function<void()>;

    DisplayManager();
    ~DisplayManager();

    void start();
    void stop();

    void registerPreRefreshCallback(RefreshCallback cb);
    void registerPostRefreshCallback(RefreshCallback cb);

    void presentFrame(PlaneHandle plane,
                      std::vector<DmaBufferPtr> buffers,
                      std::shared_ptr<void> holder);
    PlaneHandle createPlane(const PlaneConfig& config);
    std::pair<uint32_t, uint32_t> getCurrentScreenSize() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};


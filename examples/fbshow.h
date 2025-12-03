#pragma once
#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>
#include <numeric>

#include "rga/rgaProcessor.h"
#include "v4l2/cameraController.h"
#include "dma/dmaBuffer.h"
#include "drm/drmLayer.h"
#include "drm/planesCompositor.h"
#include "safeQueue.h"
#include "objectsPool.h"
#include "fenceWatcher.h"

#include "mouse/watcher.h"
#include "fileUtils.h"

#define USE_RGA_PROCESSOR 0

extern int virSave(void *data, size_t buffer_size);
extern int dmabufTest();
extern int layerTest();
extern int drmDevicesControllerTest();
extern int rgaTest();

class ComprehensiveAnalyzer {
private:
    // 理论帧率计算
    std::chrono::steady_clock::time_point m_lastProcessingTime;
    int m_processingFrameCount = 0;
    double m_theoreticalFps = 0.0;
    
    // 实际帧率计算
    std::chrono::steady_clock::time_point m_lastDisplayTime;
    int m_displayFrameCount = 0;
    double m_actualFps = 0.0;
    
    // 详细耗时统计
    std::vector<int64_t> m_queueTimes;
    std::vector<int64_t> m_dmaTimes;
    std::vector<int64_t> m_updateTimes;
    std::vector<int64_t> m_commitTimes;
    std::vector<int64_t> m_totalProcessingTimes;
    
    std::chrono::steady_clock::time_point m_lastLogTime;
    
public:
    ComprehensiveAnalyzer() {
        m_lastProcessingTime = m_lastDisplayTime = m_lastLogTime = std::chrono::steady_clock::now();
    }
    
    void markProcessingStart() {
        m_lastProcessingTime = std::chrono::steady_clock::now();
    }
    
    void markProcessingEnd(int64_t queueTime, int64_t dmaTime, int64_t updateTime, int64_t commitTime, int64_t totalTime) {
        m_processingFrameCount++;
        
        // 记录详细耗时
        m_queueTimes.push_back(queueTime);
        m_dmaTimes.push_back(dmaTime);
        m_updateTimes.push_back(updateTime);
        m_commitTimes.push_back(commitTime);
        m_totalProcessingTimes.push_back(totalTime);
        
        // 计算理论帧率（基于处理耗时）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLogTime).count();
        
        if (elapsed >= 1000) {
            // 计算理论FPS（基于处理能力）
            int64_t avgProcessingTime = std::accumulate(m_totalProcessingTimes.begin(), m_totalProcessingTimes.end(), 0LL) / m_totalProcessingTimes.size();
            m_theoreticalFps = 1000000.0 / avgProcessingTime;
            
            // 计算实际FPS（基于显示）
            m_actualFps = m_displayFrameCount * 1000.0 / elapsed;
            
            // 输出完整性能报告
            printComprehensiveReport();
            
            // 重置计数器
            m_processingFrameCount = 0;
            m_displayFrameCount = 0;
            m_queueTimes.clear();
            m_dmaTimes.clear();
            m_updateTimes.clear();
            m_commitTimes.clear();
            m_totalProcessingTimes.clear();
            m_lastLogTime = now;
        }
    }
    
    void markFrameDisplayed() {
        m_displayFrameCount++;
    }
    
    void printComprehensiveReport() {
        // 计算各阶段平均耗时
        int64_t avgQueueTime = std::accumulate(m_queueTimes.begin(), m_queueTimes.end(), 0LL) / m_queueTimes.size();
        int64_t avgDmaTime = std::accumulate(m_dmaTimes.begin(), m_dmaTimes.end(), 0LL) / m_dmaTimes.size();
        int64_t avgUpdateTime = std::accumulate(m_updateTimes.begin(), m_updateTimes.end(), 0LL) / m_updateTimes.size();
        int64_t avgCommitTime = std::accumulate(m_commitTimes.begin(), m_commitTimes.end(), 0LL) / m_commitTimes.size();
        int64_t avgTotalTime = std::accumulate(m_totalProcessingTimes.begin(), m_totalProcessingTimes.end(), 0LL) / m_totalProcessingTimes.size();
        
        printf("\n=== 完整性能分析报告 ===\n");
        printf("实际显示帧率: %.2f FPS\n", m_actualFps);
        printf("理论处理帧率: %.2f FPS\n", m_theoreticalFps);
        printf("性能利用率: %.1f%%\n", (m_actualFps / m_theoreticalFps) * 100);
        
        printf("\n--- 详细耗时分析 ---\n");
        printf("队列等待: %lldus (%.1f%%)\n", avgQueueTime, (avgQueueTime * 100.0 / avgTotalTime));
        printf("DMA处理: %lldus (%.1f%%)\n", avgDmaTime, (avgDmaTime * 100.0 / avgTotalTime));
        printf("缓冲区更新: %lldus (%.1f%%)\n", avgUpdateTime, (avgUpdateTime * 100.0 / avgTotalTime));
        printf("提交操作: %lldus (%.1f%%)\n", avgCommitTime, (avgCommitTime * 100.0 / avgTotalTime));
        printf("单帧总处理: %lldus\n", avgTotalTime);
        
        // 瓶颈分析
        printf("\n--- 瓶颈诊断 ---\n");
        if (m_actualFps < 25.0) {
            printf("🔴 帧率不足: ");
            if (m_actualFps > 19.5 && m_actualFps < 20.5) {
                printf("锁定在20FPS模式\n");
            } else {
                printf("仅 %.1f FPS\n", m_actualFps);
            }
            
            if (m_theoreticalFps > 1000.0 && m_actualFps < 30.0) {
                printf("💡 理论处理能力充足，瓶颈在显示流水线\n");
            } else if (m_theoreticalFps < 30.0) {
                printf("💡 理论处理能力不足\n");
            }
        } else {
            printf("✅ 帧率正常: %.1f FPS\n", m_actualFps);
        }
        
        // 检查各阶段占比
        std::vector<std::pair<std::string, int64_t>> stages = {
            {"队列等待", avgQueueTime},
            {"DMA处理", avgDmaTime},
            {"缓冲区更新", avgUpdateTime},
            {"提交操作", avgCommitTime}
        };
        
        auto maxStage = *std::max_element(stages.begin(), stages.end(), 
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        if (maxStage.second > avgTotalTime * 0.3) { // 超过30%
            printf("💡 主要瓶颈: %s (占%.1f%%)\n", maxStage.first.c_str(), 
                    (maxStage.second * 100.0 / avgTotalTime));
        }
    }
};

class FrameBufferTest{
    // 16.16 定位
    uint32_t fx(uint32_t v){ return v << 16; }
public:
    // 释放资源(devices/planes)
    void preRefresh(){
        refreshing = true;
        
        // 停止所有活动
        #if USE_RGA_PROCESSOR
        processor->pause();
        #endif
        cctr->pause();
        
        // 移除所有图层
        compositor->removeAllLayer();
    }

    void postRefresh(){
        auto infoPrinter = [](const std::vector<uint32_t>& Ids){
            std::cout << "Gain " << Ids.size() <<" usable planes";
            for(auto& id : Ids){
                std::cout << " " << id;
            }
            std::cout << ".\n";
        };
        auto initLayer = [this](std::shared_ptr<DrmLayer>& layer, DrmLayer::LayerProperties& layerProps){
            // 设置属性
            layer->setProperty(layerProps);
            // 设置更新回调
            layer->setUpdateCallback([this](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId){
                // 更新 fb
                compositor->updateLayer(layer, fbId);
            });
        };

        // 获取设备组合
        devices = &(DrmDev::fd_ptr->getDevices());
        if (devices->empty()){
            std::cout << "Get no devices." << std::endl;
            return;
        }
        // 取出第一个屏幕
        dev = (*devices)[0];
        std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
            << ", Resolution: " << dev->width << "x" << dev->height << "\n";

        // 获取所有在指定CRTC上的Plane
        DrmDev::fd_ptr->refreshPlane(dev->crtc_id);
        // 初始化 id 列表
        std::vector<uint32_t> usableCursorPlaneIds;
        std::vector<uint32_t> usableOverlayPlaneIds;
        // 获取指定类型并且支持目标格式的 Plane DRM_FORMAT_NV12
        DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_PRIMARY, DRM_FORMAT_ABGR8888, usableCursorPlaneIds);
        DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, formatRGAtoDRM(dstFormat), usableOverlayPlaneIds);
        infoPrinter(usableCursorPlaneIds);
        infoPrinter(usableOverlayPlaneIds);
        // return -1; // 查询所有格式时用

        if (usableCursorPlaneIds.empty() || usableOverlayPlaneIds.empty())// 若无可以plane则退出
        { std::cout << "Some plane do not matched.\n"; return; }
        mouseMonitor.setScreenSize(dev->width, dev->height);
        frameLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );
        cursorLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 1) );
        // 配置属性
        DrmLayer::LayerProperties frameLayerProps{
            .plane_id_   = usableOverlayPlaneIds[0],  // 取支持NV12的第一个overlay plane
            .crtc_id_    = dev->crtc_id,

            // 源图像区域
            // src_* 使用左移 16
            .srcX_       = fx(0),
            .srcY_       = fx(0),
            .srcwidth_   = fx(cctrCfg.width),
            .srcheight_  = fx(cctrCfg.height),
            // 显示图像区域
            // crtc_* 不使用左移
            .crtcX_      = 0,
            .crtcY_      = 0,
            // 自动缩放
            .crtcwidth_  = dev->width,
            .crtcheight_ = dev->height,
            .zOrder_     = 0 // 置于底层
        };

        DrmLayer::LayerProperties cursorLayerProps{
            .plane_id_   = usableCursorPlaneIds[0],
            .crtc_id_    = dev->crtc_id,
            // 源区域: 64x64 的光标图标
            .srcX_       = fx(0),
            .srcY_       = fx(0),
            .srcwidth_   = fx(CURSOR_SIZE),
            .srcheight_  = fx(CURSOR_SIZE),
            // 显示区域: 64x64, 初始位置在 (0,0)
            .crtcX_      = 0,
            .crtcY_      = 0,
            .crtcwidth_  = CURSOR_SIZE,
            .crtcheight_ = CURSOR_SIZE,
            .zOrder_     = 2
        };
        // 初始化layer
        initLayer(frameLayer, frameLayerProps);
        initLayer(cursorLayer, cursorLayerProps);
        // 将layer添加到合成器
        compositor->addLayer(frameLayer);
        compositor->addLayer(cursorLayer);
        std::cout << "Layer initialized.\n"; 
        // 重新获取资源后重启
        cctr->start();
        loadCursorIcon("./cursor-64.png");
        refreshing = false;
    }

    explicit FrameBufferTest(){
        // 创建队列
// 准备思路: v4l2捕获后图像直接显示到DRM上, 若开启推理才让RGA实际跑起来
        rawFrameQueue  	= std::make_shared<FrameQueue>(31);
        
        // 相机配置
        cctrCfg = CameraController::Config {
            .buffer_count = 25,
            .plane_count = 2,
            .use_dmabuf = true,
            .device = "/dev/video0",
            .width = 3840,
            .height = 2160,
            // .width = 1280,
            // .height = 720,
            .format = cctrFormat
        };
        
        // 初始化相机控制器
        cctr = std::make_shared<CameraController>(cctrCfg);
        if (!cctr) {
            std::cout << "Failed to create CameraController object.\n";
            return;
        }
        // 设置入队队列
        cctr->setFrameCallback([this](FramePtr f) {
            rawFrameQueue->enqueue(std::move(f));
        });

        // 导出合成器
        compositor = std::move(PlanesCompositor::create());
        if (!compositor){
            std::cout << "Failed to create PlanesCompositor object.\n";
            return;
        }

        // 转移顺序,先释放资源再重新获取
        DrmDev::fd_ptr->registerResourceCallback(
            std::bind(&FrameBufferTest::preRefresh, this),
            std::bind(&FrameBufferTest::postRefresh, this)
        );
        postRefresh(); // 初始刷新
    }

    ~FrameBufferTest(){
        stop();
    }

    void start(){
        if (running) return;
        running.store(true);
        mouseMonitor.start();

        mthread_ = std::thread(&FrameBufferTest::cursorLoop, this);
        thread_ = std::thread(&FrameBufferTest::run, this);
    }

    void stop(){
        // 手动停止后析构依旧会调用导致隐藏的二次析构问题, 因此添加判断
        if (!running) return; 
        running.store(false);
        
        mouseMonitor.stop();
        fprintf(stdout, "Mouse monitor stopped.\n");
        if (mthread_.joinable()) mthread_.join();
        fprintf(stdout, "Mouse thread stopped.\n");
        if (thread_.joinable()) thread_.join();
        fprintf(stdout, "Frame processing thread stopped.\n");

        cctr->stop();
        devices = nullptr;
    }

private:
    // 线程实现
    void run(){              
        int totalFrames = 0;
        int waitRefreshCount = 0;
        int waitQueueCount = 0;
        ComprehensiveAnalyzer analyzer;
        
        while (running) {
            auto loopStart = std::chrono::steady_clock::now();
                        
            // 刷新等待
            if (true == refreshing) {
                waitRefreshCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            FramePtr frame;
            std::vector<DmaBufferPtr> buffers;
            
            // 队列检查
            auto queueStart = std::chrono::steady_clock::now();
            if (!rawFrameQueue->try_dequeue(frame)) {
                waitQueueCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            analyzer.markProcessingStart();
            
            auto queueEnd = std::chrono::steady_clock::now();
            auto queueTime = std::chrono::duration_cast<std::chrono::microseconds>(queueEnd - queueStart).count();
            
            // DMA缓冲区处理
            auto dmaStart = std::chrono::steady_clock::now();
            auto Y_buf = frame->sharedState(0)->dmabuf_ptr;
            auto UV_buf = DmaBuffer::importFromFD(
                Y_buf->fd(),
                Y_buf->width(),
                Y_buf->height() / 2,
                Y_buf->format(),
                Y_buf->pitch() * Y_buf->height() / 2,
                Y_buf->pitch() * Y_buf->height()
            );
            buffers.emplace_back(std::move(Y_buf));
            buffers.emplace_back(std::move(UV_buf));
            auto dmaEnd = std::chrono::steady_clock::now();
            auto dmaTime = std::chrono::duration_cast<std::chrono::microseconds>(dmaEnd - dmaStart).count();
            
            // 更新缓冲区
            auto updateStart = std::chrono::steady_clock::now();
            frameLayer->updateBuffer(std::move(buffers));
            auto updateEnd = std::chrono::steady_clock::now();
            auto updateTime = std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count();

            // 提交
            auto commitStart = std::chrono::steady_clock::now();
            int fence = -1;
            compositor->commit(fence);
            auto commitEnd = std::chrono::steady_clock::now();
            auto commitTime = std::chrono::duration_cast<std::chrono::microseconds>(commitEnd - commitStart).count();
                        
            // Fence监听 - 实际显示时间点
            FenceWatcher::instance().watchFence(fence, [&analyzer, this](){
                frameLayer->onFenceSignaled();
                analyzer.markFrameDisplayed();
            });
            totalFrames++;

            // 总处理时间
            auto loopEnd = std::chrono::steady_clock::now();
            auto totalTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count();
            
            // 记录处理完成
            analyzer.markProcessingEnd(queueTime, dmaTime, updateTime, commitTime, totalTime);
        }
    }
    // 鼠标光标
    void cursorLoop(){
        int x = 0, y = 0;           
        while (running) {
            // 等待刷新完成
            if (refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (!mouseMonitor.getPosition(x, y)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // 计算边界裁剪
            uint32_t crtc_x = static_cast<uint32_t>(std::max(0, x));
            uint32_t crtc_y = static_cast<uint32_t>(std::max(0, y));
            
            // 计算实际可显示的宽高
            uint32_t visible_width = CURSOR_SIZE;
            uint32_t visible_height = CURSOR_SIZE;
            uint32_t src_x = 0;
            uint32_t src_y = 0;
            
            // 右边界检测
            if (x + CURSOR_SIZE > static_cast<int>(dev->width)) {
                visible_width = dev->width - x;
            }
            
            // 底边界检测
            if (y + CURSOR_SIZE > static_cast<int>(dev->height)) {
                visible_height = dev->height - y;
            }
            
            // 左边界检测（如果x为负）
            if (x < 0) {
                src_x = -x;
                visible_width = CURSOR_SIZE + x;
                crtc_x = 0;
            }
            
            // 上边界检测（如果y为负）
            if (y < 0) {
                src_y = -y;
                visible_height = CURSOR_SIZE + y;
                crtc_y = 0;
            }
            
            // 更新光标图层属性
            cursorLayer->setProperty("x", fx(src_x));
            cursorLayer->setProperty("y", fx(src_y));
            cursorLayer->setProperty("w", fx(visible_width));
            cursorLayer->setProperty("h", fx(visible_height));
            cursorLayer->setProperty("crtcX", crtc_x);
            cursorLayer->setProperty("crtcY", crtc_y);
            cursorLayer->setProperty("crtcW", visible_width);
            cursorLayer->setProperty("crtcH", visible_height);
            
            // 提交更新
            compositor->updateLayer(cursorLayer);
        }
    }
    
    // 加载光标图像
    void loadCursorIcon(const std::string& iconPath) {
        auto cursorIcon = std::move(readImage(iconPath, DRM_FORMAT_ABGR8888));
        if (!cursorIcon) {
            std::cout << "Failed to create cursor DmaBuffer.\n";
            return;
        }
        cursorLayer->updateBuffer({ cursorIcon });
        
        // 验证 FB ID
        auto fb_id = cursorLayer->getProperty("fbId").get<uint32_t>();
        if (fb_id == 0) {
            fprintf(stderr, "ERROR: Cursor fb_id is 0! updateBuffer failed.\n");
            return;
        }
        fprintf(stdout, "Cursor layer created: %dx%d, format=ARGB8888, fb_id=%u\n",
            CURSOR_SIZE, CURSOR_SIZE, fb_id);
    }

    // 光标尺寸
    const uint32_t CURSOR_SIZE = 64;
    // 资源管理
    std::atomic_bool refreshing{false};
    SharedDev* devices;
    DevPtr dev;
    // 帧队列
    std::shared_ptr<FrameQueue> rawFrameQueue, frameQueue;
    // 相机配置
    uint32_t cctrFormat = V4L2_PIX_FMT_NV12;
    int dstFormat = RK_FORMAT_YCbCr_420_SP;
    CameraController::Config cctrCfg{};
    std::shared_ptr<CameraController> cctr;
    // 合成器
    std::unique_ptr<PlanesCompositor> compositor;
    // 层
    std::shared_ptr<DrmLayer> frameLayer;   // 在 overlay 的帧显示layer
    std::shared_ptr<DrmLayer> cursorLayer;  // 在 cursor 上显示的layer
    // 鼠标监控
    MouseWatcher mouseMonitor;
    // 主线程
    std::atomic_bool running{false};
    std::thread thread_, mthread_;
};
#pragma once
#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>

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
        rawFrameQueue  	= std::make_shared<FrameQueue>(2);
        
        // 相机配置
        cctrCfg = CameraController::Config {
            .buffer_count = 2,
            .plane_count = 2,
            .use_dmabuf = true,
            .device = "/dev/video0",
            // .width = 3840,
            // .height = 2160,
            .width = 1280,
            .height = 720,
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
        while (running) {
            // 等待刷新完成
            if (true == refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            FramePtr frame; // 自动释放drmbuf
            std::vector<DmaBufferPtr> buffers;
            if (!rawFrameQueue->try_dequeue(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            // 取出该帧的drmbuf
            auto dmabuf = frame->sharedState(0)->dmabuf_ptr;
            auto dmabuf2 = DmaBuffer::importFromFD(
                dmabuf->fd(),
                dmabuf->width(),
                dmabuf->height(),
                dmabuf->format(),
                dmabuf->size(),
                dmabuf->width() * dmabuf->height()
            );
            buffers.emplace_back(std::move(dmabuf));
            buffers.emplace_back(std::move(dmabuf2));
            // 更新fb,同时回调触发合成器更新fbid
            frameLayer->updateBuffer(std::move(buffers));
            // 提交一次
            int fence = -1;
            compositor->commit(fence);
            // 监听fence
            FenceWatcher::instance().watchFence(fence, [this](){
                frameLayer->onFenceSignaled();
            });
        }
    }

    void cursorLoop(){
        int x = 0, y = 0;           
        while (running) {
            // 等待刷新完成
            if (refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            if (!mouseMonitor.getPosition(x, y)) {
                fprintf(stderr, "1");
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

    void loadCursorIcon(const std::string& iconPath) {
        // 加载光标图像
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
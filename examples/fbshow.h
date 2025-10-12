#pragma once
#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>

#include "threadUtils.h"
#include "rga/rgaProcessor.h"
#include "v4l2/cameraController.h"
#include "dma/dmaBuffer.h"
#include "drm/drmLayer.h"
#include "drm/planesCompositor.h"
#include "fenceWatcher.h"
#include "safeQueue.h"
#include "objectsPool.h"
#include "rander/draw.h"
#include "rander/core.h"

extern int virSave(void *data, size_t buffer_size);
extern int dmabufTest();
extern int layerTest();
extern int drmDevicesControllerTest();
extern int rgaTest();


class FrameBufferTest{
    // 资源管理
    std::atomic_bool refreshing{false};
    SharedDev* devices;
    DevPtr dev;
    // 帧队列
    std::shared_ptr<FrameQueue> rawFrameQueue, frameQueue;
    // 相机配置
    uint32_t cctrFormat = V4L2_PIX_FMT_NV12;
    CameraController::Config cctrCfg{};
    std::shared_ptr<CameraController> cctr;
    // rga配置
    int dstFormat = RK_FORMAT_RGBA_8888;
    uint32_t width = 2560;
    uint32_t height = 1440;
    int format = -1;
    int poolSize = 0;
    RgaProcessor::Config rgaCfg{};
    std::shared_ptr<RgaProcessor> processor;
    // 合成器
    std::unique_ptr<PlanesCompositor> compositor;
    // 层
    std::shared_ptr<DrmLayer> frameLayer; // 在primary的帧显示layer
    std::shared_ptr<DrmLayer> overLayer;  // 在overlay上显示的layer
    // 主线程
    std::atomic_bool running{false};
    std::thread thread_;
    // 16.16 定位
    uint32_t fx(uint32_t v){ return v << 16; }
public:
    // 释放资源(devices/planes)
    void preRefresh(){
        refreshing = true;
        
        // 停止所有活动
        processor->pause();
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
        std::vector<uint32_t> usablePrimaryPlaneIds;
        std::vector<uint32_t> usableOverlayPlaneIds;
        // 获取指定类型并且支持目标格式的 Plane
        DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_PRIMARY, formatRGAtoDRM(dstFormat), usablePrimaryPlaneIds);
        DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, formatRGAtoDRM(dstFormat), usableOverlayPlaneIds);
        infoPrinter(usablePrimaryPlaneIds);
        infoPrinter(usableOverlayPlaneIds);
        // return -1; // 查询所有格式时用

        if (usablePrimaryPlaneIds.empty() || usableOverlayPlaneIds.empty())// 若无可以plane则退出
        { std::cout << "Some plane do not matched.\n"; return; }

        frameLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );
        overLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );
        // 配置属性
        DrmLayer::LayerProperties frameLayerProps{
            .plane_id_   = usablePrimaryPlaneIds[0],  
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
            .crtcheight_ = dev->height
        };
        // 配置属性
        DrmLayer::LayerProperties overLayerProps = frameLayerProps;
        overLayerProps.plane_id_ = usableOverlayPlaneIds[0];
        initLayer(frameLayer, frameLayerProps);
        initLayer(overLayer, overLayerProps);
        
        // 将layer添加到合成器
        compositor->addLayer(frameLayer);
        compositor->addLayer(overLayer);
        std::cout << "Layer initialized.\n"; 
        // 重新获取资源后重启
        cctr->start();
        cctr->setThreadAffinity(0); // 绑定到核心0
        processor->start();
        processor->setThreadAffinity(3); // 绑定到核心2
        refreshing = false;
    }

    explicit FrameBufferTest(){
        // 创建队列
// 准备思路: v4l2捕获后图像直接显示到DRM上, 若开启推理才让RGA实际跑起来
        rawFrameQueue  	= std::make_shared<FrameQueue>(2);
        frameQueue     	= std::make_shared<FrameQueue>(2);
        
        // 相机配置
        cctrCfg = CameraController::Config {
            .buffer_count = 2,
            .plane_count = 2,
            .use_dmabuf = true,
            .device = "/dev/video0",
            // .width = 3840,
            // .height = 2160,
            .width = width,
            .height = height,
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

        // 初始化转换线程
        poolSize = static_cast<int>( frameQueue->getBufferRealSize() );
        format = (V4L2_PIX_FMT_NV12 == cctrFormat) ?
            RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;
        rgaCfg = RgaProcessor::Config {
            cctr, rawFrameQueue, cctrCfg.width,
            cctrCfg.height, cctrCfg.use_dmabuf, dstFormat, format, poolSize
        };
        processor = std::make_shared<RgaProcessor>(rgaCfg) ;

        // 导出合成器
        compositor = std::move(PlanesCompositor::create());
        if (!compositor){
            std::cout << "Failed to create PlanesCompositor object.\n";
            return;
        }

        // 初始化layer
        frameLayer = std::make_shared<DrmLayer>(std::vector<DmaBufferPtr>(), 2);

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
        thread_ = std::thread(&FrameBufferTest::run, this);
    }

    void stop(){
        running.store(false);
        if (thread_.joinable()){
            thread_.join();
        }
        processor->stop();
        cctr->stop();
        devices = nullptr;
    }

private:
    // 线程实现
    void run(){
        ThreadUtils::bindCurrentThreadToCore(0);

        struct timeval time;
        gettimeofday(&time, nullptr);
        auto startTime = time.tv_sec * 1000 + time.tv_usec / 1000;
    
        int frames = 0;
        float fps = 0.0;
        auto beforeTime = startTime;

        bool usingRknn = false; 
        auto& core = Core::instance();
        core.registerResSlot("yolo", 2, width, height, formatRGAtoDRM(dstFormat), width * height * 4, 0);
        int i = 0;
        FramePtr frame;
        // 出队帧缓存
        while (running) {
            // 等待刷新完成
            if (true == refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // 取出一帧
            if (processor->dump(frame) < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            int OpenGLFence = -1;
            int DRMFence = -1;
            // 取出该帧的drmbuf
            auto frameBuf = frame->sharedState()->dmabuf_ptr;
            if (nullptr == frameBuf) {
                std::cout << "Failed to get dmabuf from frame.\n";
                continue;
            }
            auto slot = core.acquireFreeSlot("yolo");
            if (nullptr == slot) {
                std::cout << "Failed to acquire slot.\n";
                continue;
            }
            // 清空并绘制不同的内容
            QString text = QString("Fps: %1/s").arg(fps);
            Draw::instance().drawText(*(slot.get()), text, QPointF(10, 45), Qt::red);
            i++;
            // 同步内容到 dmabuf
            if (!slot->syncToDmaBuf(OpenGLFence)) {
                std::cout << "Failed to sync dmabuf. \n";
                core.releaseSlot("test", slot);
                continue;
            };
            if (slot->dmabufPtr == nullptr) {
                std::cout << "Slot dmabuf is null.\n";
                core.releaseSlot("test", slot);
                continue;
            }

            // 等待绘制和显示
            FenceWatcher::instance().watchFence(OpenGLFence, [frameBuf, this, &DRMFence, slot]() {
                // 更新fb,同时回调触发合成器更新fbid
                frameLayer->updateBuffer({frameBuf});
                overLayer->updateBuffer({slot->dmabufPtr});
                compositor->commit(DRMFence);
                FenceWatcher::instance().watchFence(DRMFence, [this]() {
                    overLayer->onFenceSignaled();
                    frameLayer->onFenceSignaled();
                });
            });
            frames++;
            
            if (frames % 10 == 0)
            {
                gettimeofday(&time, nullptr);
                auto currentTime = time.tv_sec * 1000 + time.tv_usec / 1000;
                fps = 10.0 / float(currentTime - beforeTime) * 1000.0;
                // printf("10帧内平均帧率:\t %f fps/s\n", fps);
                beforeTime = currentTime;
            }

            frame.reset();
            core.releaseSlot("yolo", slot);
        }
        Draw::instance().shutdown();
        Core::instance().shutdown();
    }
};
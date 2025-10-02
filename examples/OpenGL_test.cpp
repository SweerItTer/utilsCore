/*
 * @FilePath: /EdgeVision/examples/OpenGL_test.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-05-21 19:21:51
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "ConfigInterface/offerScreenWidget.h"
#include <QApplication>

#include <sys/syscall.h>
#include <csignal>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>

#include "threadUtils.h"			// 线程绑定
#include "rga/rgaProcessor.h"		// rga 处理
#include "v4l2/cameraController.h"	// v4l2 视频采集
#include "dma/dmaBuffer.h"			// dmabuf 管理
#include "drm/drmLayer.h"			// layer 管理
#include "drm/planesCompositor.h"	// plane 管理
#include "fenceWatcher.h"			// fence 监视
#include "safeQueue.h"				// 安全队列
#include "objectsPool.h"			// 对象池
#include "rander/core.h"			// 渲染核心
#include "rander/draw.h"			// 绘制方法

static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running = false;
    }
}
auto chooseClosestResolution(int screenW, int screenH) -> std::pair<int, int>{
	
    static const std::vector<std::pair<int, int>> standardRes = {
        {640, 480}, {720, 480}, {720, 576}, {1280, 720},
        {1920, 1080}, {2560, 1440}//, {3840, 2160}, {4096, 2160}
    };

    std::pair<int, int> bestRes;
    int minDist = std::numeric_limits<int>::max();

    for (const auto& res : standardRes) {
        // 使用平方距离，越小越接近屏幕大小
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


class FrameBufferTest{
public:

	void cpInit() {		
		// 获取实际屏幕输出大小
		auto captureRes = chooseClosestResolution(dev->width, dev->height);
		width = captureRes.first;
		height = captureRes.second;
		// 捕获配置
		cctrCfg = {
			.buffer_count = 2,
			.plane_count = 2,
			.use_dmabuf = true,
			.device = "/dev/video0",
			.width = width,
			.height = height,
			.format = cctrFormat
		};
		
		// 初始化视频捕获类
		cctr.reset( new CameraController(cctrCfg) );
		if (!cctr) { std::cout << "Failed to create CameraController object.\n"; return; }
		// 设置回调入队队列
		cctr->setFrameCallback([this](FramePtr f) {
			rawFrameQueue->enqueue(std::move(f));
		});

		auto poolSize = static_cast<int>( frameQueue->getBufferRealSize() ); // 线程池长度
		auto format = (V4L2_PIX_FMT_NV12 == cctrCfg.format) ?
			RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;			// 原始数据格式
		rgaCfg = {
			cctr, rawFrameQueue, frameQueue,
			cctrCfg.width,
			cctrCfg.height,
			cctrCfg.use_dmabuf, dstFormat, format, poolSize
		};
		processor.reset( new RgaProcessor(rgaCfg) );			// 初始化RGA转换类
	}

	// 构造函数
	explicit FrameBufferTest(){
		// 初始化UI界面
		mainInterface = std::make_shared<MainInterface>();
		// 创建原始NV12帧队列和RGBA帧队列
		rawFrameQueue  	= std::make_shared<FrameQueue>(2);
		frameQueue     	= std::make_shared<FrameQueue>(2);
		
		// 导出合成器
		compositor = std::move(PlanesCompositor::create());
		if (!compositor){ std::cout << "Failed to create PlanesCompositor object.\n"; return; }

		// 热插拔回调, 先释放资源再重新获取
		DrmDev::fd_ptr->registerResourceCallback(
			std::bind(&FrameBufferTest::preRefresh, this),
			std::bind(&FrameBufferTest::postRefresh, this)
		);
		postRefresh(); // 初始刷新
	}

    // 释放资源(devices/planes)
    void preRefresh(){
        refreshing = true; 
		// 停止所有活动
		processor->pause();
		cctr->pause();
		rawFrameQueue->clear(); // 清空队列
		frameQueue->clear();
		// 析构线程
		processor.reset();
		cctr.reset();
        // 移除所有图层
        compositor->removeAllLayer();
		
		devices->clear(); // 清空设备组合
		dev.reset(); // 清空当前设备组合
	}

	// 重新获取资源(devices/planes)
    void postRefresh(){
        // 获取设备组合
        devices = &(DrmDev::fd_ptr->getDevices());
        if (devices->empty()){
            std::cout << "Get no devices." << std::endl;
            return;
        }
        // 取出第一个可用设备组合
        dev = (*devices)[0];
        std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
            << ", Resolution: " << dev->width << "x" << dev->height << "\n";

		cpInit();	// 重新配置主要线程
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

		// 初始化 layer
        frameLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );	// 双缓冲
        overLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );
        // 配置属性
        DrmLayer::LayerProperties frameLayerProps{
            .plane_id_   = usablePrimaryPlaneIds[0],  
            .crtc_id_    = dev->crtc_id,

            // 源图像区域
            // src_* 使用左移 16
            .srcX_       = fx(0),
            .srcY_       = fx(0),
            .srcwidth_   = fx(width),
            .srcheight_  = fx(height),
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
        processor->setThreadAffinity(2); // 绑定到核心2
        refreshing = false;
    }

    ~FrameBufferTest(){
        stop();
		devices = nullptr;
    }

    void start(){
        if (running) return;
        running.store(true);
        thread_ = std::thread(&FrameBufferTest::run, this);
		threadUI_ = std::thread(&FrameBufferTest::threadUI, this);
    }

    void stop(){
        running.store(false);
        if (thread_.joinable()){
            thread_.join();
        }
		if (threadUI_.joinable()){
            threadUI_.join();
        }
        processor->stop();
        cctr->stop();
    }

private:
	void threadUI(){
		// 绑定UI绘制线程到CPU CORE 3
		ThreadUtils::bindCurrentThreadToCore(3);
		auto& core = Core::instance();// 获取渲染 core
		auto& draw = Draw::instance();// 获取绘制 draw
		std::string slotType = "UI&Yolo";	// slot命名

		auto updateSlot = [ this, &slotType](){
			auto dmabufTemplate = DmaBuffer::create(	// 创建 dmabuf 模板
				width, height, formatRGAtoDRM(dstFormat),
				width * height * 4, 0);
			if (!dmabufTemplate){
				std::cout << "Failed to create dmabuf template.\n";
				return;
			}
			Core::instance().registerResSlot(slotType, 2, std::move(dmabufTemplate)); // 注册 slot
		};
		updateSlot();				// 初始化 slot		
		bool needUpdate = false;	// slot 更新标志
		while(running){
			// 等待刷新完成
            if (true == refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				needUpdate = true;
                continue;
            }
			if (needUpdate){ // 重新获取 slot
				updateSlot();
				needUpdate = false;
			}
			int OpenGLFence = -1;
			auto slot = core.acquireFreeSlot(slotType);
            if (nullptr == slot) {
                std::cout << "Failed to acquire slot.\n";
                continue;
            }
            // 清空并绘制不同的内容
            QString text = QString("Fps: %1/s").arg(fps.load());
			QRect targetRect(10, 50, dev->width/3 , dev->height/2);
			draw.clear(slot->qfbo.get());												// 清空画布
			draw.drawText(*(slot.get()), text, QPointF(10, 45), QColor(255, 0, 0));		// 绘制帧率
			draw.drawWidget(*(slot.get()), mainInterface.get(), targetRect, RenderMode::KeepAspectRatio);	// 绘制UI界面

			// 同步内容到 dmabuf
            if (!slot->syncToDmaBuf(OpenGLFence)) {
                std::cout << "Failed to sync dmabuf. \n";
                core.releaseSlot(slotType, slot);
                continue;
            };
            if (slot->dmabufPtr == nullptr) {
                std::cout << "Slot dmabuf is null.\n";
                core.releaseSlot(slotType, slot);
                continue;
            }
			// 等待绘制完成
            FenceWatcher::instance().watchFence(OpenGLFence, [this, slot]() {
				overLayer->updateBuffer({slot->dmabufPtr});
			});
			core.releaseSlot(slotType, slot);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		// 关闭渲染资源
		Draw::instance().shutdown();
		Core::instance().shutdown();
	}

    // 线程实现
    void run(){
		// 绑定主线程到CPU CORE 0
        ThreadUtils::bindCurrentThreadToCore(0);
		std::cout << "DRM show thread TID: " << syscall(SYS_gettid) << "\n";
		// 帧率计算用
		int frames = 0;
        struct timeval time;
        gettimeofday(&time, nullptr);
        auto startTime = time.tv_sec * 1000 + time.tv_usec / 1000;
        auto beforeTime = startTime;
		
		uint64_t frameId = 0;
        while (running) {
            // 等待刷新完成
            if (true == refreshing) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
			// 取出的帧
			FramePtr frame;

            // 取出一帧
            if (!frameQueue->try_dequeue(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            // 取出该帧的drmbuf
            DmaBufferPtr& frameBuf = frame->sharedState()->dmabuf_ptr;
			if (frameId < frame->meta.frame_id){
				frameId = frame->meta.frame_id;
			} else {
				// 遇到旧帧则丢弃
				std::cout << "Drop old frame " << frame->meta.frame_id << ".\n";
				continue;
			}
            if (nullptr == frameBuf) {
                std::cout << "Failed to get dmabuf from frame.\n";
                continue;
            }

            int DRMFence = -1;
			// 更新fb, 同时回调触发合成器更新fbid
			frameLayer->updateBuffer({frameBuf});
			compositor->commit(DRMFence);
			FenceWatcher::instance().watchFence(DRMFence, [this]() {
				frameLayer->onFenceSignaled();
				overLayer->onFenceSignaled();
			});
			
            frames++;
			// 计算平均帧率
            if (frames % 10 == 0) {
                gettimeofday(&time, nullptr);
                auto currentTime = time.tv_sec * 1000 + time.tv_usec / 1000;
                fps.store(10.0 / float(currentTime - beforeTime) * 1000.0);
                beforeTime = currentTime;
            }
		}
    }

	// planes 信息输出
	void infoPrinter(const std::vector<uint32_t>& Ids){
		std::cout << "Gain " << Ids.size() <<" usable planes";
		for(auto& id : Ids){
			std::cout << " " << id;
		}
		std::cout << ".\n";
	};
	// 初始化 layer
	void initLayer(std::shared_ptr<DrmLayer>& layer, DrmLayer::LayerProperties& layerProps){
		// 设置属性
		layer->setProperty(layerProps);
		// 设置更新回调
		layer->setUpdateCallback([this](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId){
			// 更新 fb
			compositor->updateLayer(layer, fbId);
		});
	};
private:
	std::atomic<float> fps{0.0};
	std::thread threadUI_;

	// 资源管理
	std::atomic_bool refreshing{false};	// 刷新标志
	SharedDev* devices;					// 设备组合列表
	DevPtr dev;							// 具体组合
	// UI 界面
	std::shared_ptr<MainInterface> mainInterface = nullptr; 
	// 帧队列
	std::shared_ptr<FrameQueue> rawFrameQueue, frameQueue;
	// 相机配置
	uint32_t width = 2560;
	uint32_t height = 1440;
	uint32_t cctrFormat = V4L2_PIX_FMT_NV12;	// 采集图像格式
	CameraController::Config cctrCfg{};			// 配置
	std::shared_ptr<CameraController> cctr;		// 图像采集类
	// rga配置
	int dstFormat = RK_FORMAT_RGBA_8888;		// 图像转换后输出格式
	RgaProcessor::Config rgaCfg{};				// 配置
	std::shared_ptr<RgaProcessor> processor;	// RGA图像处理类
	// 合成器
	std::unique_ptr<PlanesCompositor> compositor;
	// 层
	std::shared_ptr<DrmLayer> frameLayer; 		// 在primary的帧显示layer
	std::shared_ptr<DrmLayer> overLayer;  		// 在overlay上显示的layer
	// 主线程
	std::atomic_bool running{false};			// 主线程运行标志
	std::thread thread_;						// 线程
	// 16.16 定位
	uint32_t fx(uint32_t v){ return v << 16; }
};

int main(int argc, char *argv[])
{
	// qt 相关初始化
	QApplication app(argc, argv);
	std::signal(SIGINT, handleSignal);
	DrmDev::fd_ptr = DeviceController::create(); // 初始化全局唯一fd_ptr
	FrameBufferTest test;
	test.start();
	// int a = 5;
	// while (a--)
	// {
	// 	sleep(1);
	// }
	while (running)
	{
		sleep(1000);
	}
	test.stop();
	return 0;
}

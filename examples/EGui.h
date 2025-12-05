#pragma once
#include "ConfigInterface/maininterface.h"
#include <QApplication>
#include <QTimer>

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
#include "rknnPool.h"		    	// rknn 模型池
#include "yolov5s.h"				// yolov5s 模型
#include "dma/dmaBuffer.h"			// dmabuf 管理
#include "drm/drmLayer.h"			// layer 管理
#include "drm/planesCompositor.h"	// plane 管理
#include "fenceWatcher.h"			// fence 监视
#include "asyncThreadPool.h"		// 异步线程池
#include "objectsPool.h"			// 对象池
#include "qMouseWatch.h"
#include "rander/core.h"			// 渲染核心
#include "rander/draw.h"			// 绘制方法
#include "sys/cpuMonitor.h"         // 资源检测
#include "sys/memoryMonitor.h"

// 选择最接近屏幕分辨率的标准分辨率
static auto chooseClosestResolution(int screenW, int screenH) -> std::pair<int, int> {
    static const std::vector<std::pair<int, int>> standardRes = {
        {640, 480}, {720, 480}, {720, 576}, {1280, 720},
        {1920, 1080}, {2560, 1440}, {3840, 2160}, {4096, 2160}
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
    // 16.16 定位
	uint32_t fx(uint32_t v){ return v << 16; }
    QMouseWatch mouseWatcher;   // 继承自MouseWatcher的Qt事件特供类
public:
    // 构造函数
	explicit FrameBufferTest();
    // 析构
    ~FrameBufferTest();

    // 流水线初始化
    void initVisionPipeline();
	// 摄像头
    void cameraInit();
    // RGA 初始化
    void rgaInit();

    /* QT 主线程(堵塞)
     * 不调用需要主动堵塞主线程保证视频显示
     */
	void RunUI(int &argc, char **argv);
	    
    // 启动线程
    void start();
    // 关闭并释放资源
    void stop();

private:
    // 图像显示主线程
    void run();
    
    // 释放资源(devices/planes)
    void preRefresh();
	// 重新获取资源(devices/planes)
    void postRefresh();
    
	// planes 信息输出
	void infoPrinter(const std::vector<uint32_t>& Ids);
	// 初始化 layer
	void initLayer(std::shared_ptr<DrmLayer>& layer, DrmLayer::LayerProperties& layerProps);

private:
    // 主线程
    std::atomic_bool refreshing{false};	        // 刷新标志
	std::atomic_bool running{false};			// 主线程运行标志
	std::thread thread_;						// 线程
	std::atomic<float> fps{0.0};                // 视频捕获线程FPS标志

	// UI 界面
	std::shared_ptr<MainInterface> mainInterface;
    // rknn 线程池
    std::shared_ptr<rknnPool<Yolov5s, DmaBufferPtr, object_detect_result_list>> rknnPool_;

    // 帧队列
    std::shared_ptr<FrameQueue> rawFrameQueue, forRGAFrameQueue;
    // 相机配置
    CameraController::Config cameraConfig{};	// 相机配置
    uint32_t captureFormat = V4L2_PIX_FMT_NV12;	// 采集图像格式
    uint32_t autoWidth, autoHeight;             // 自适应屏幕大小匹配合理的捕获分辨率 
    std::shared_ptr<CameraController> cameraCapturer; // 视频采集
    // rga配置
    RgaProcessor::Config rgaCfg;
    int primaryFormat = RK_FORMAT_RGBA_8888;	// 输出图像格式 -> 供 rknn 使用
    int overlayFormat = RK_FORMAT_YCbCr_420_SP; // 输入图像格式 -> 来自 v4l2(nv12)
    std::shared_ptr<RgaProcessor> processor;    // rga图像处理
    
	// DRM 资源管理
	SharedDev* devices;					// 可用设备组合列表
	DevPtr dev;							// 具体选用设备组合
    // 层
	std::shared_ptr<DrmLayer> overLayer; 		// overlay上显示NV12图像
	std::shared_ptr<DrmLayer> primaryLayer;  	// primary上显示UI等绘制元素
	// 合成器
	std::unique_ptr<PlanesCompositor> compositor;
	// 资源监视器
    CpuMonitor CPUMonitor;
    MemoryMonitor MMonitor;
};

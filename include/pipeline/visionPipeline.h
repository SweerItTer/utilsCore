/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:57:28
 * @FilePath: /EdgeVision/include/pipeline/visionPipeline.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <memory>
#include <iostream>

// 摄像头数据流
#include "rga/rgaProcessor.h"		// rga 处理
#include "v4l2/cameraController.h"  // 摄像头捕获

// 录像拍照
#include "mpp/encoderCore.h"
#include "mpp/jpegEncoder.h"
#include "mpp/streamWriter.h"

// 工具类
#include "types.h"                  // 帧包装
#include "dma/dmaBuffer.h"          // DMABUF 

class VisionPipeline {
public:
    using RGACallBack = std::function<void(DmaBufferPtr, std::shared_ptr<void>)>;
    enum class RecordStatus {
        Start = 0,
        Stop
    };
    enum class ModelStatus {
        Start = 0,
        Stop
    };
    // 默认配置
    static CameraController::Config defaultCameraConfig(
        uint32_t width=0,
        uint32_t height=0, 
        uint32_t format=V4L2_PIX_FMT_NV12);

    VisionPipeline(const CameraController::Config& cameraConfig);
    ~VisionPipeline();
// -------------- 重新配置 --------------
    void resetConfig(const CameraController::Config& newConfig);

// -------------- 流水线控制 --------------
    void start(); // 启动
    void stop();  // 停止
    void pause(); // 暂停
    void resume(); // 恢复

// -------------- 摄像头控制 --------------
    void setMirrorMode(bool horizontal, bool vertical);
    void setExposurePercentage(float percentage);

// -------------- 编码控制 --------------
    bool tryCapture();    // 拍照
    bool tryRecord(RecordStatus stauts);    // 录像

// -------------- 模型推理控制 --------------
    bool setModelRunningStatus(ModelStatus stauts);
    void registerOnRGA(RGACallBack cb_);

// -------------- 数据/信息获取 --------------
    bool getCurrentRawFrame(FramePtr& frame);
    bool getCurrentRGAFrame(FramePtr& frame);
    float getFPS();
    int getCameraFd();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

inline CameraController::Config VisionPipeline::defaultCameraConfig(
    uint32_t width,
    uint32_t height, 
    uint32_t format)
{
    if ((0 < width) && (0 < height)) {
        uint32_t aligned_width  = (0 == (width  % 8)) ? width  : ((width  + 7) & ~7);
        uint32_t aligned_height = (0 == (height % 8)) ? height : ((height + 7) & ~7);

        if ((aligned_width != width) || (aligned_height != height)) {
            std::cout << "[defaultCameraConfig] align to "
                      << aligned_width << "x" << aligned_height
                      << " from " << width << "x" << height << std::endl;
        }

        width  = aligned_width;
        height = aligned_height;
    } else {
        width  = 1920;
        height = 1080;

        std::cout << "[defaultCameraConfig] invalid input size, fallback to "
                  << width << "x" << height << std::endl;
    }

    CameraController::Config dfConfig{
        .buffer_count = 4,
        .plane_count  = 1,
        .use_dmabuf   = true,
        .device       = "/dev/video0",
        .width        = width,
        .height       = height,
        .format       = format
    };

    return dfConfig;
}
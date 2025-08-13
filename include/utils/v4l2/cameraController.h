/*
 * @FilePath: /EdgeVision/include/utils/v4l2/cameraController.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-05 00:43:04
 * @LastEditors: Please set LastEditors
 */

/* 
 * PIMPL 隐藏实现细节 减少编译时间(原本修改后 头文件和源文件都需要重新编译,此后只编译源文件)
 */

#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <functional>
#include <memory>
#include <string>
#include <linux/videodev2.h>

#include "frame.h"
#include "sharedBufferState.h"

class CameraController {
public:
    // 定义 FrameCallback 类型(更简洁,简化std::function<void(Frame)>声明)
    // 接受的参数是 Frame,返回类型为 void
    using FrameCallback = std::function<void(Frame)>;
    
    struct Config {
        // 默认配置
        int buffer_count = 4;
        __u32 plane_count = 2; // plane 个数
        bool use_dmabuf = false;    // 默认使用MMAP
        std::string device = "/dev/video0";
        uint32_t width = 1280;
        uint32_t height = 720;
        uint32_t format = V4L2_PIX_FMT_NV12;
    };
    
    explicit CameraController(const Config& config);
    ~CameraController();
    
    void start();
    void pause();
    void stop();

    void returnBuffer(int index);
    
    // 帧回调
    void setFrameCallback(FrameCallback&& callback);
    // 获取设备 fd
    int getDeviceFd() const;
    
    // 禁用拷贝和移动
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // !CAMERA_CONTROLLER_H
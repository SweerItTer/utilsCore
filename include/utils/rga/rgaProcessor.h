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

struct RgbaBuffer {
    void* data = nullptr;
    bool in_use = false;
};

class RgaProcessor {
public:
    RgaProcessor(std::shared_ptr<CameraController> cctr = nullptr,
                std::shared_ptr<FrameQueue> rawQueue = nullptr,
                std::shared_ptr<FrameQueue> outQueue = nullptr,
                int width = 0, int height = 0,
                int dstFormat = RK_FORMAT_RGBA_8888, int srcformat = RK_FORMAT_YCbCr_420_SP,
                int poolSize = 4
                );

    ~RgaProcessor();

    void start();
    void stop();
    void releaseBuffer(int index);

private:
    void run();

    std::atomic_bool running_;
    std::thread worker_;

    std::shared_ptr<FrameQueue> rawQueue_;
    std::shared_ptr<FrameQueue> outQueue_;
    std::shared_ptr<CameraController> cctr_;
    RgaConverter converter_;
    
    int width_;
    int height_;
    int dstFormat_;
    int srcFormat_;

    std::vector<RgbaBuffer> bufferPool_;
    int currentIndex_ = 0;
    const int poolSize_ = 0;
};
    

#endif // RGAPROCESSTHREAD_H

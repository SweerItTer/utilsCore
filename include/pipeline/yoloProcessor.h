/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date:   2025-12-14
 * @FilePath: /EdgeVision/include/pipeline/yoloProcessor.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <thread>

#include "m_types.h"
#include "dma/dmaBuffer.h"

class YoloProcessor {
public:
    using ResultType  = object_detect_result_list;
    using ResultCB    = std::function<void(ResultType)>;

public:
// YoloProcessor(const std::string& modelPath="./yolov5s_relu.rknn",
//     const std::string& classesTxtPath="./coco_80_labels_list.txt",
//     const size_t poolSize=5
// );
    YoloProcessor(
        const std::string& modelPath,
        const std::string& classesTxtPath,
        const size_t poolSize
    );
    ~YoloProcessor();

    void start();
    void stop();
    void pause();
    void resume();

    void setThresh(float BOX_THRESH_=-1.0f, float thNMS_THRESHresh_=-1.0f);

    // 输入 RGB (来自 VisionPipeline)
    void submit(DmaBufferPtr rgb, std::shared_ptr<void> holder);
    // YOLO 推理回调
    void setOnResult(ResultCB cb);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
 
 
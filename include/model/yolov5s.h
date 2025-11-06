/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-13 21:53:21
 * @FilePath: /EdgeVision/include/model/yolov5s.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include "m_types.h"
#include "yolov5.h"
#include "preprocess.h"
#include "postprocess.h"
#include "objectsPool.h"

#include <memory>

class Yolov5s
{
public:

    Yolov5s(const std::string &modelPath, const std::string &COCOPath,
        float NMS_THRESH = 0.45, float BOX_THRESH = 0.25, AnchorSet anchorSet = postprocess::anchors);
    ~Yolov5s() = default;

    int init(rknn_app_context& inCtx, bool isChild);

    rknn_app_context& getCurrentContext();

    DmaBufferPtr infer(DmaBufferPtr inDmabuf, bool drawText);
    object_detect_result_list infer(DmaBufferPtr inDmabuf);

private:
    int drawBox(const object_detect_result_list &results, DmaBufferPtr outBuf, bool drawText);
private:
    std::string modelPath_;
    rknn_app_context appCtx;
    float confThres = 0;
    float iouThresh = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    AnchorSet anchorSet_;
    rknn_io_tensor_mem* mem = nullptr;
    std::vector<std::string> classes;
    std::unique_ptr<ObjectPool<DmaBufferPtr>> outBufPool;
};
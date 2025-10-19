/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 23:46:28
 * @FilePath: /EdgeVision/include/model/postprocess.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <vector>
#include <array>
#include <string>
#include "yolov5.h"
#include "m_types.h"

namespace postprocess {    
    // static std::vector<std::string> class_names;   
    static const AnchorSet anchors = {
        {{10, 13},  {16, 30},   {33, 23}},
        {{30, 61},  {62, 45},   {59, 119}},
        {{116, 90}, {156, 198}, {373, 326}}
    };
    /* 特征层是尺度, anchor 是形状模板, 层数和每层 anchor 数量都由训练时决定
    特征层 (layer) 如: stride=8(常见8,16,32) { 
        实际大小 = anchor * stride
        anchor 1 (w,h) {10, 13} // 实际大小 (80, 104)
        anchor 2 (w,h) {16, 30} // 实际大小 (128, 240)
        anchor 3 (w,h) {33, 23} // 实际大小 (264, 184)
    } */
    
    bool read_class_names(const std::string& path,
        std::vector<std::string>& class_names);
    int post_process_rule(
        rknn_app_context& app_ctx,              // rknn上下文
        rknn_tensor_mem* out_mem[],             // 输出mem
        letterbox& lb,                          // 填充box
        std::vector<std::string>& class_names,  // label信息
        object_detect_result_list& results,     // 返回结果
        float conf_thresh = 0.25,               // 置信度阈值 
        float iou_thresh = 0.45,                // NMS阈值
        AnchorSet anchors = anchors             // 可选anchors结构和数量
    );
} // namespace postprocess
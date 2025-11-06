/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-10 06:38:24
 * @FilePath: /EdgeVision/include/model/m_types.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <vector>
#include <string>
struct letterbox{
    int x_pad;
    int y_pad;
    float scale;
};

struct rect{
    int left;
    int top;
    int right;
    int bottom;
};

struct rect_pos{
    float x, y, w, h;
};

// 目标结果列表
struct object_detect_result {
    rect_pos box;       // 目标box (x, y, w, h)
    float prop;         // 目标概率
    int class_id;       // 目标类别ID
    std::string class_name;
};

struct Anchor {
    float w;
    float h;
};

using AnchorLayer = std::vector<Anchor>;    
using AnchorSet = std::vector<AnchorLayer>;
using object_detect_result_list = std::vector<object_detect_result>;    

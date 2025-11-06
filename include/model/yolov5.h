/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 20:04:55
 * @FilePath: /EdgeVision/include/model/yolov5.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include "rknn/rknn_api.h"
#include "fileUtils.h"

struct rknn_io_tensor_mem{
    DmaBufferPtr input_buf = nullptr;
    rknn_tensor_mem* input_mems[1]  = {};     // 输入tensor内存
    rknn_tensor_mem* output_mems[3] = {};     // 输出tensor内存
};

struct rknn_app_context {
    rknn_context rknn_ctx {0};              // rknn上下文
    rknn_input_output_num io_num {};        // 输入输出数量

    int model_channel {0};                  // 模型输入通道数
    int model_width {0};                    // 模型输入宽度
    int model_height {0};                   // 模型输入高度
    bool is_quant {false};                  // 是否量化模型

    std::vector<rknn_tensor_attr> input_attrs;   // 输入tensor属性
    std::vector<rknn_tensor_attr> output_attrs;  // 输出tensor属性

    rknn_io_tensor_mem io_mem;
    
    rknn_app_context() = default;
    ~rknn_app_context();

    // 禁用拷贝
    rknn_app_context(const rknn_app_context&) = delete;
    rknn_app_context& operator=(const rknn_app_context&) = delete;

    // 移动
    rknn_app_context(rknn_app_context&& other) noexcept;
    rknn_app_context& operator=(rknn_app_context&& other) noexcept;
};

// 加载 RKNN 模型
int loadModel(const char* model_path, rknn_app_context& app_ctx);

// 加载 IO 信息
int loadIOnum(rknn_app_context& app_ctx);

// 初始化 IO mems
int initializeMems(rknn_app_context& app_context);

// // 获取可用mem
// rknn_io_tensor_mem* get_usable_mem(std::vector<rknn_io_tensor_mem> &pool);

// // 释放使用的mem
// void release_mem(rknn_io_tensor_mem* mem_slot);
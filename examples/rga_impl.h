/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-05 17:45:12
 * @FilePath: /EdgeVision/examples/rga_impl.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <algorithm>

#include "dma/dmaBuffer.h"
#include "rga/rgaConverter.h"
#include "m_types.h" // 假设包含 DmaBufferPtr, rect, letterbox 等定义

// 封装 RGA 的核心操作
int rga_process_core(const DmaBufferPtr& src, const DmaBufferPtr& dst, 
                     rect* src_box, rect* dst_box, char color) {
    
    // 1. 获取基础参数
    int src_w = src->width();
    int src_h = src->height();
    int src_pitch = src->pitch();
    int dst_w = dst->width();
    int dst_h = dst->height();
    int dst_pitch = dst->pitch();

    // 2. 计算 stride (RGA 需要以像素为单位的步长)
    // 假设是 RGB888 格式，每个像素 3 字节
    int src_wstride = src_pitch / 3;
    int dst_wstride = dst_pitch / 3;

    // 3. 包装 RGA Buffer
    rga_buffer_t src_rgabuf = wrapbuffer_fd(src->fd(), src_w, src_h, RK_FORMAT_RGB_888, src_wstride, src_h);
    rga_buffer_t dst_rgabuf = wrapbuffer_fd(dst->fd(), dst_w, dst_h, RK_FORMAT_RGB_888, dst_wstride, dst_h);

    // 4. 定义处理区域 (Rect)
    im_rect src_rect = {src_box->left, src_box->top, 
                        src_box->right - src_box->left + 1, 
                        src_box->bottom - src_box->top + 1};
    im_rect dst_rect = {dst_box->left, dst_box->top, 
                        dst_box->right - dst_box->left + 1, 
                        dst_box->bottom - dst_box->top + 1};

    // 5. RGA 硬件执行：背景填充
    im_rect whole_dst_rect = {0, 0, dst_w, dst_h};
    RgaConverter::instance().ImageFill(dst_rgabuf, whole_dst_rect, color);

    // 6. RGA 硬件执行：缩放并拷贝到目标位置
    RgaConverter::RgaParams params = {src_rgabuf, src_rect, dst_rgabuf, dst_rect};
    rga_buffer_t pat{}; im_rect prect{}; int usage = 0;
    IM_STATUS ret = RgaConverter::instance().ImageProcess(params, pat, prect, usage);

    return (ret == IM_STATUS_SUCCESS) ? 0 : -1;
}
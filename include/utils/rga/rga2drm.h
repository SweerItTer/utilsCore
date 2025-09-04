/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-04 01:33:35
 * @FilePath: /EdgeVision/include/utils/rga/rga2drm.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <unordered_map>
#include <drm/drm_fourcc.h>
#include "rga/rgaConverter.h"

/*
    RGA采用 低地址 到 高地址: B | G | R | A 来命名
    DRM使用 fourcc_code 刚刚好相反,从高到低 A | R | G | B
    我nm
*/
// RGA->DRM 格式映射表
static const std::unordered_map<int, uint32_t> rgaToDrmMap = {
    // --- RGB / ARGB ---
    {RK_FORMAT_RGB_565,    DRM_FORMAT_RGB565},
    {RK_FORMAT_RGB_888,    DRM_FORMAT_RGB888},     // 24bit
    {RK_FORMAT_BGR_888,    DRM_FORMAT_BGR888},
    {RK_FORMAT_RGBA_8888,  DRM_FORMAT_ABGR8888},   // RGA:RGBA → DRM:ABGR
    {RK_FORMAT_BGRA_8888,  DRM_FORMAT_ARGB8888},   // RGA:BGRA → DRM:ARGB
    {RK_FORMAT_ARGB_8888,  DRM_FORMAT_BGRA8888},   // RGA:ARGB → DRM:BGRA
    {RK_FORMAT_ABGR_8888,  DRM_FORMAT_RGBA8888},   // RGA:ABGR → DRM:RGBA
    {RK_FORMAT_XRGB_8888,  DRM_FORMAT_BGRX8888},
    {RK_FORMAT_XBGR_8888,  DRM_FORMAT_RGBX8888},
    {RK_FORMAT_RGBX_8888,  DRM_FORMAT_XBGR8888},
    {RK_FORMAT_BGRX_8888,  DRM_FORMAT_XRGB8888},

    // --- YUV 4:2:0 ---
    {RK_FORMAT_YCbCr_420_SP, DRM_FORMAT_NV12},  // NV12
    {RK_FORMAT_YCrCb_420_SP, DRM_FORMAT_NV21},  // NV21
    {RK_FORMAT_YCbCr_420_P,  DRM_FORMAT_YUV420},// I420
    {RK_FORMAT_YCrCb_420_P,  DRM_FORMAT_YVU420},// YV12

    // --- YUV 4:2:2 ---
    {RK_FORMAT_YCbCr_422_SP, DRM_FORMAT_NV16},  
    {RK_FORMAT_YCrCb_422_SP, DRM_FORMAT_NV61},  
    {RK_FORMAT_YCbCr_422_P,  DRM_FORMAT_YUV422}, 
    {RK_FORMAT_YCrCb_422_P,  DRM_FORMAT_YVU422}
};

// 转换函数
inline uint32_t formatRGAtoDRM(int rgaFmt) {
    auto it = rgaToDrmMap.find(rgaFmt);
    if (it != rgaToDrmMap.end()) {
        return it->second;
    }
    return DRM_FORMAT_INVALID; // 未找到对应格式
}

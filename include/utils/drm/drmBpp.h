/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-04 01:48:29
 * @FilePath: /EdgeVision/include/utils/drm/drmBpp.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DRM_BPP_H
#define DRM_BPP_H

#include <linux/videodev2.h>

#include <xf86drm.h>            // DRM 核心功能
#include <xf86drmMode.h>        // DRM 模式设置功能
#include <drm/drm_fourcc.h>     // DRM 格式定义
#include <stdexcept>            // 需要抛异常
#include <string>               // 添加string支持
#include <unordered_map>
#include <cstdint>
// 头文件内实现函数需要加上 inline避免多次展开出现重定义错误
// 对于头文件实现的类的成员函数,是隐式 inline 所以是安全的

// ========== 映射表定义 ==========

// DRM bpp
static const std::unordered_map<uint32_t, uint32_t> drmBppMap = {
    {DRM_FORMAT_R8, 8}, {DRM_FORMAT_C8, 8},
    {DRM_FORMAT_RGB565, 16}, {DRM_FORMAT_BGR565, 16}, {DRM_FORMAT_NV16, 16},
    {DRM_FORMAT_RGB888, 24}, {DRM_FORMAT_BGR888, 24},
    {DRM_FORMAT_ARGB8888, 32}, {DRM_FORMAT_XRGB8888, 32},
    {DRM_FORMAT_ABGR8888, 32}, {DRM_FORMAT_XBGR8888, 32},
    {DRM_FORMAT_RGBA8888, 32}, {DRM_FORMAT_RGBX8888, 32},
    {DRM_FORMAT_BGRA8888, 32}, {DRM_FORMAT_BGRX8888, 32},
    {DRM_FORMAT_NV12, 12}, {DRM_FORMAT_NV21, 12},
    {DRM_FORMAT_YUYV, 16}
};

// V4L2 to DRM
static const std::unordered_map<uint32_t, uint32_t> v4l2ToDrmMap = {
    {V4L2_PIX_FMT_NV12, DRM_FORMAT_NV12},
    {V4L2_PIX_FMT_NV16, DRM_FORMAT_NV16},
    {V4L2_PIX_FMT_RGB24, DRM_FORMAT_RGB888},
    {V4L2_PIX_FMT_YUYV, DRM_FORMAT_YUYV},
    {V4L2_PIX_FMT_XRGB32, DRM_FORMAT_XRGB8888}
};

// ========== 函数实现 ==========

// 根据 DRM 格式查 bpp
inline uint32_t calculate_bpp(uint32_t format) {
    auto it = drmBppMap.find(format);
    return (it != drmBppMap.end()) ? it->second : -1; // 未知返回0
}

// // V4L2 格式 → bpp
// inline uint32_t get_bpp(uint32_t pixelformat) {
//     auto it = v4l2ToDrmMap.find(pixelformat);
//     return (it != v4l2ToDrmMap.end()) ? calculate_bpp(it->second) : -1;
// }

// V4L2 格式 转 DRM 格式
inline uint32_t convertV4L2ToDrmFormat(uint32_t v4l2_fmt) {
    auto it = v4l2ToDrmMap.find(v4l2_fmt);
    return (it != v4l2ToDrmMap.end()) ? it->second : -1;
}

#endif // DRM_BPP_H

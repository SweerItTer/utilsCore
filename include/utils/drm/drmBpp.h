/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-19
 * @FilePath: /EdgeVision/include/utils/drm/drmBpp.h
 * @Description: 支持多平面格式的 bpp 计算和索引访问
 */
#ifndef DRM_BPP_H
#define DRM_BPP_H

#include <linux/videodev2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cstdint>

// ---------------- PlaneRatio ----------------
struct PlaneRatio {
    float w; // width factor
    float h; // height factor
    PlaneRatio(float w_, float h_) : w(w_), h(h_) {}
};

// ---------------- PlaneFormatInfo ----------------
struct PlaneFormatInfo {
    std::vector<PlaneRatio> planes;

    PlaneRatio operator()(size_t idx) const {
        if (idx >= planes.size()) throw std::out_of_range("Invalid plane index");
        return planes[idx];
    }
    
    size_t planeCount() const { return planes.size(); }
};

// ---------------- DRM bpp 映射 ----------------
static const std::unordered_map<uint32_t, uint32_t> drmBppMap = {
    {DRM_FORMAT_R8, 8}, {DRM_FORMAT_C8, 8},
    {DRM_FORMAT_RGB565, 16}, {DRM_FORMAT_BGR565, 16}, {DRM_FORMAT_NV16, 16},
    {DRM_FORMAT_RGB888, 24}, {DRM_FORMAT_BGR888, 24},
    {DRM_FORMAT_ARGB8888, 32}, {DRM_FORMAT_XRGB8888, 32},
    {DRM_FORMAT_ABGR8888, 32}, {DRM_FORMAT_XBGR8888, 32},
    {DRM_FORMAT_RGBA8888, 32}, {DRM_FORMAT_RGBX8888, 32},
    {DRM_FORMAT_BGRA8888, 32}, {DRM_FORMAT_BGRX8888, 32},
    {DRM_FORMAT_NV12, 8}, {DRM_FORMAT_NV21, 8},
    {DRM_FORMAT_YUYV, 16}
};

// ---------------- 多平面信息 map ----------------
static const std::unordered_map<uint32_t, PlaneFormatInfo> drmPlaneMap = {
    // 多平面格式
    {DRM_FORMAT_NV12, PlaneFormatInfo{{PlaneRatio(1.0f,1.5f)}}},  // 特殊处理(单buf连续存储)
    {DRM_FORMAT_NV21, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f), PlaneRatio(1.0f,0.5f)}}},
    {DRM_FORMAT_NV16, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f), PlaneRatio(1.0f,1.0f)}}},
    // 单平面格式
    {DRM_FORMAT_R8, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_C8, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_RGB565, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_BGR565, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_RGB888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_BGR888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_ARGB8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_XRGB8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_ABGR8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_XBGR8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_RGBA8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_RGBX8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_BGRA8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}},
    {DRM_FORMAT_BGRX8888, PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}}
};

// ---------------- V4L2 转 DRM ----------------
static const std::unordered_map<uint32_t,uint32_t> v4l2ToDrmMap = {
    {V4L2_PIX_FMT_NV12, DRM_FORMAT_NV12},
    {V4L2_PIX_FMT_NV21, DRM_FORMAT_NV21},
    {V4L2_PIX_FMT_NV16, DRM_FORMAT_NV16},
    {V4L2_PIX_FMT_NV61, DRM_FORMAT_NV16}, // NV61 同 NV16
    {V4L2_PIX_FMT_YUYV, DRM_FORMAT_YUYV},
    {V4L2_PIX_FMT_RGB24, DRM_FORMAT_RGB888},
    {V4L2_PIX_FMT_BGR24, DRM_FORMAT_BGR888},
    {V4L2_PIX_FMT_XRGB32, DRM_FORMAT_XRGB8888},
    {V4L2_PIX_FMT_ARGB32, DRM_FORMAT_ARGB8888},
    {V4L2_PIX_FMT_ABGR32, DRM_FORMAT_ABGR8888},
};

// ---------------- 计算 bpp ----------------
inline uint32_t calculate_bpp(uint32_t format) {
    auto it = drmBppMap.find(format);
    return (it != drmBppMap.end()) ? it->second : 0;
}

// ---------------- 获取 plane info ----------------
inline PlaneFormatInfo getPlaneInfo(uint32_t format) {
    auto it = drmPlaneMap.find(format);
    if (it != drmPlaneMap.end()) return it->second;
    return PlaneFormatInfo{{PlaneRatio(1.0f,1.0f)}}; // 默认单层
}

// ---------------- V4L2 转 DRM 格式 ----------------
inline uint32_t convertV4L2ToDrmFormat(uint32_t v4l2_fmt) {
    auto it = v4l2ToDrmMap.find(v4l2_fmt);
    return (it != v4l2ToDrmMap.end()) ? it->second : 0;
}

#endif // DRM_BPP_H

/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-04 01:33:35
 * @FilePath: /EdgeVision/include/utils/rga/formatTool.h
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
static const std::unordered_map<int, uint32_t> rgaToDrmFormat = {
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

// DRM -> RGA 格式映射表
static const std::unordered_map<uint32_t, int> drmToRgaFormat = {
    // --- RGB / ARGB ---
    {DRM_FORMAT_RGB565,   RK_FORMAT_RGB_565},
    {DRM_FORMAT_RGB888,   RK_FORMAT_RGB_888},
    {DRM_FORMAT_BGR888,   RK_FORMAT_BGR_888},

    {DRM_FORMAT_ABGR8888, RK_FORMAT_RGBA_8888},  // DRM:ABGR → RGA:RGBA
    {DRM_FORMAT_ARGB8888, RK_FORMAT_BGRA_8888},  // DRM:ARGB → RGA:BGRA
    {DRM_FORMAT_BGRA8888, RK_FORMAT_ARGB_8888},  // DRM:BGRA → RGA:ARGB
    {DRM_FORMAT_RGBA8888, RK_FORMAT_ABGR_8888},  // DRM:RGBA → RGA:ABGR

    {DRM_FORMAT_BGRX8888, RK_FORMAT_XRGB_8888},
    {DRM_FORMAT_RGBX8888, RK_FORMAT_XBGR_8888},
    {DRM_FORMAT_XBGR8888, RK_FORMAT_RGBX_8888},
    {DRM_FORMAT_XRGB8888, RK_FORMAT_BGRX_8888},

    // --- YUV 4:2:0 ---
    {DRM_FORMAT_NV12,     RK_FORMAT_YCbCr_420_SP},
    {DRM_FORMAT_NV21,     RK_FORMAT_YCrCb_420_SP},
    {DRM_FORMAT_YUV420,   RK_FORMAT_YCbCr_420_P},
    {DRM_FORMAT_YVU420,   RK_FORMAT_YCrCb_420_P},

    // --- YUV 4:2:2 ---
    {DRM_FORMAT_NV16,     RK_FORMAT_YCbCr_422_SP},
    {DRM_FORMAT_NV61,     RK_FORMAT_YCrCb_422_SP},
    {DRM_FORMAT_YUV422,   RK_FORMAT_YCbCr_422_P},
    {DRM_FORMAT_YVU422,   RK_FORMAT_YCrCb_422_P}
};

// DRM -> RGA 转换函数
inline int convertDRMtoRGAFormat(uint32_t drmFmt) {
    auto it = drmToRgaFormat.find(drmFmt);
    if (it != drmToRgaFormat.end()) {
        return it->second;
    }
    return -1; // 未找到对应格式
}

// RGA -> DRM 转换函数
inline uint32_t convertRGAtoDrmFormat(int rgaFmt) {
    auto it = rgaToDrmFormat.find(rgaFmt);
    if (it != rgaToDrmFormat.end()) {
        return it->second;
    }
    return -1; // 未找到对应格式
}

// ---------------------------

// V4L2 -> RGA 格式映射表
static const std::unordered_map<uint32_t, int> v4l2ToRgaFormat = {
    // --- RGB ---
    {V4L2_PIX_FMT_RGB565,  RK_FORMAT_RGB_565},
    {V4L2_PIX_FMT_RGB24,   RK_FORMAT_RGB_888},
    {V4L2_PIX_FMT_BGR24,   RK_FORMAT_BGR_888},

    {V4L2_PIX_FMT_ARGB32,  RK_FORMAT_ARGB_8888},
    {V4L2_PIX_FMT_ABGR32,  RK_FORMAT_ABGR_8888},

    // --- YUV 4:2:0 ---
    {V4L2_PIX_FMT_NV12,    RK_FORMAT_YCbCr_420_SP},
    {V4L2_PIX_FMT_NV21,    RK_FORMAT_YCrCb_420_SP},
    {V4L2_PIX_FMT_YUV420,  RK_FORMAT_YCbCr_420_P},
    {V4L2_PIX_FMT_YVU420,  RK_FORMAT_YCrCb_420_P},

    // --- YUV 4:2:2 ---
    {V4L2_PIX_FMT_NV16,    RK_FORMAT_YCbCr_422_SP},
    {V4L2_PIX_FMT_NV61,    RK_FORMAT_YCrCb_422_SP},
    {V4L2_PIX_FMT_YUYV,    RK_FORMAT_YUYV_422},
    {V4L2_PIX_FMT_UYVY,    RK_FORMAT_UYVY_422}
};

// RGA -> V4L2 格式映射表
static const std::unordered_map<int, uint32_t> rgaToV4l2Format = {
    // --- RGB ---
    {RK_FORMAT_RGB_565,    V4L2_PIX_FMT_RGB565},
    {RK_FORMAT_RGB_888,    V4L2_PIX_FMT_RGB24},
    {RK_FORMAT_BGR_888,    V4L2_PIX_FMT_BGR24},

    {RK_FORMAT_ARGB_8888,  V4L2_PIX_FMT_ARGB32},
    {RK_FORMAT_ABGR_8888,  V4L2_PIX_FMT_ABGR32},

    // --- YUV 4:2:0 ---
    {RK_FORMAT_YCbCr_420_SP, V4L2_PIX_FMT_NV12},
    {RK_FORMAT_YCrCb_420_SP, V4L2_PIX_FMT_NV21},
    {RK_FORMAT_YCbCr_420_P,  V4L2_PIX_FMT_YUV420},
    {RK_FORMAT_YCrCb_420_P,  V4L2_PIX_FMT_YVU420},

    // --- YUV 4:2:2 ---
    {RK_FORMAT_YCbCr_422_SP, V4L2_PIX_FMT_NV16},
    {RK_FORMAT_YCrCb_422_SP, V4L2_PIX_FMT_NV61},
    {RK_FORMAT_YUYV_422,     V4L2_PIX_FMT_YUYV},
    {RK_FORMAT_UYVY_422,     V4L2_PIX_FMT_UYVY}
};

// V4L2 -> RGA
inline int convertV4L2toRGAFormat(uint32_t v4l2Fmt) {
    auto it = v4l2ToRgaFormat.find(v4l2Fmt);
    if (it != v4l2ToRgaFormat.end()) {
        return it->second;
    }
    return -1;
}

// RGA -> V4L2
inline uint32_t convertRGAtoV4L2Format(int rgaFmt) {
    auto it = rgaToV4l2Format.find(rgaFmt);
    if (it != rgaToV4l2Format.end()) {
        return it->second;
    }
    return -1;
}
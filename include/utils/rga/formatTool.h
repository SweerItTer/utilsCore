/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-04 01:33:35
 * @FilePath: /include/utils/rga/formatTool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: RGA格式转换工具 - 优化版本
 */
#pragma once

#include <cstdint>
#include <unordered_map>
#include <drm/drm_fourcc.h>
#include "rga/rgaConverter.h"

namespace utils {
namespace rga {

/**
 * @brief 格式转换错误码
 */
enum class FormatError : int {
    SUCCESS = 0,
    INVALID_FORMAT = -1,
    UNSUPPORTED_FORMAT = -2
};

/**
 * @brief RGA和DRM格式字节序说明
 * 
 * RGA采用低地址到高地址: B | G | R | A 命名
 * DRM使用fourcc_code从高到低: A | R | G | B 命名
 * 例如: 
 * - RGA: RK_FORMAT_RGBA_8888 → DRM: DRM_FORMAT_ABGR8888
 * - RGA: RK_FORMAT_BGRA_8888 → DRM: DRM_FORMAT_ARGB8888
 */

namespace detail {
inline const std::unordered_map<int, uint32_t>& rgaToDrmFormatMap() {
    static const auto map = [] {
        std::unordered_map<int, uint32_t> m;
        m.reserve(22);
        m.max_load_factor(0.7f);

        // RGB
        m.emplace(RK_FORMAT_RGB_565, DRM_FORMAT_RGB565);
        m.emplace(RK_FORMAT_RGB_888, DRM_FORMAT_RGB888);
        m.emplace(RK_FORMAT_BGR_888, DRM_FORMAT_BGR888);
        m.emplace(RK_FORMAT_RGBA_8888, DRM_FORMAT_ABGR8888); // RGA:RGBA → DRM:ABGR
        m.emplace(RK_FORMAT_BGRA_8888, DRM_FORMAT_ARGB8888); // RGA:BGRA → DRM:ARGB
        m.emplace(RK_FORMAT_ARGB_8888, DRM_FORMAT_BGRA8888); // RGA:ARGB → DRM:BGRA
        m.emplace(RK_FORMAT_ABGR_8888, DRM_FORMAT_RGBA8888); // RGA:ABGR → DRM:RGBA
        m.emplace(RK_FORMAT_XRGB_8888, DRM_FORMAT_BGRX8888);
        m.emplace(RK_FORMAT_XBGR_8888, DRM_FORMAT_RGBX8888);
        m.emplace(RK_FORMAT_RGBX_8888, DRM_FORMAT_XBGR8888);
        m.emplace(RK_FORMAT_BGRX_8888, DRM_FORMAT_XRGB8888);

        // YUV 4:2:0
        m.emplace(RK_FORMAT_YCbCr_420_SP, DRM_FORMAT_NV12);
        m.emplace(RK_FORMAT_YCrCb_420_SP, DRM_FORMAT_NV21);
        m.emplace(RK_FORMAT_YCbCr_420_P, DRM_FORMAT_YUV420);
        m.emplace(RK_FORMAT_YCrCb_420_P, DRM_FORMAT_YVU420);

        // YUV 4:2:2
        m.emplace(RK_FORMAT_YCbCr_422_SP, DRM_FORMAT_NV16);
        m.emplace(RK_FORMAT_YCrCb_422_SP, DRM_FORMAT_NV61);
        m.emplace(RK_FORMAT_YCbCr_422_P, DRM_FORMAT_YUV422);
        m.emplace(RK_FORMAT_YCrCb_422_P, DRM_FORMAT_YVU422);

        // Special
        m.emplace(RK_FORMAT_YUYV_422, DRM_FORMAT_YUYV);
        m.emplace(RK_FORMAT_UYVY_422, DRM_FORMAT_UYVY);

        return m;
    }();
    return map;
}

inline const std::unordered_map<uint32_t, int>& drmToRgaFormatMap() {
    static const auto map = [] {
        std::unordered_map<uint32_t, int> m;
        m.reserve(22);
        m.max_load_factor(0.7f);

        // RGB
        m.emplace(DRM_FORMAT_RGB565, RK_FORMAT_RGB_565);
        m.emplace(DRM_FORMAT_RGB888, RK_FORMAT_RGB_888);
        m.emplace(DRM_FORMAT_BGR888, RK_FORMAT_BGR_888);
        m.emplace(DRM_FORMAT_ABGR8888, RK_FORMAT_RGBA_8888); // DRM:ABGR → RGA:RGBA
        m.emplace(DRM_FORMAT_ARGB8888, RK_FORMAT_BGRA_8888); // DRM:ARGB → RGA:BGRA
        m.emplace(DRM_FORMAT_BGRA8888, RK_FORMAT_ARGB_8888); // DRM:BGRA → RGA:ARGB
        m.emplace(DRM_FORMAT_RGBA8888, RK_FORMAT_ABGR_8888); // DRM:RGBA → RGA:ABGR
        m.emplace(DRM_FORMAT_BGRX8888, RK_FORMAT_XRGB_8888);
        m.emplace(DRM_FORMAT_RGBX8888, RK_FORMAT_XBGR_8888);
        m.emplace(DRM_FORMAT_XBGR8888, RK_FORMAT_RGBX_8888);
        m.emplace(DRM_FORMAT_XRGB8888, RK_FORMAT_BGRX_8888);

        // YUV 4:2:0
        m.emplace(DRM_FORMAT_NV12, RK_FORMAT_YCbCr_420_SP);
        m.emplace(DRM_FORMAT_NV21, RK_FORMAT_YCrCb_420_SP);
        m.emplace(DRM_FORMAT_YUV420, RK_FORMAT_YCbCr_420_P);
        m.emplace(DRM_FORMAT_YVU420, RK_FORMAT_YCrCb_420_P);

        // YUV 4:2:2
        m.emplace(DRM_FORMAT_NV16, RK_FORMAT_YCbCr_422_SP);
        m.emplace(DRM_FORMAT_NV61, RK_FORMAT_YCrCb_422_SP);
        m.emplace(DRM_FORMAT_YUV422, RK_FORMAT_YCbCr_422_P);
        m.emplace(DRM_FORMAT_YVU422, RK_FORMAT_YCrCb_422_P);

        // Special
        m.emplace(DRM_FORMAT_YUYV, RK_FORMAT_YUYV_422);
        m.emplace(DRM_FORMAT_UYVY, RK_FORMAT_UYVY_422);

        return m;
    }();
    return map;
}

inline const std::unordered_map<uint32_t, int>& v4l2ToRgaFormatMap() {
    static const auto map = [] {
        std::unordered_map<uint32_t, int> m;
        m.reserve(12);
        m.max_load_factor(0.7f);

        // RGB
        m.emplace(V4L2_PIX_FMT_RGB565, RK_FORMAT_RGB_565);
        m.emplace(V4L2_PIX_FMT_RGB24, RK_FORMAT_RGB_888);
        m.emplace(V4L2_PIX_FMT_BGR24, RK_FORMAT_BGR_888);
        m.emplace(V4L2_PIX_FMT_ARGB32, RK_FORMAT_ARGB_8888);
        m.emplace(V4L2_PIX_FMT_ABGR32, RK_FORMAT_ABGR_8888);

        // YUV 4:2:0
        m.emplace(V4L2_PIX_FMT_NV12, RK_FORMAT_YCbCr_420_SP);
        m.emplace(V4L2_PIX_FMT_NV21, RK_FORMAT_YCrCb_420_SP);
        m.emplace(V4L2_PIX_FMT_YUV420, RK_FORMAT_YCbCr_420_P);
        m.emplace(V4L2_PIX_FMT_YVU420, RK_FORMAT_YCrCb_420_P);

        // YUV 4:2:2
        m.emplace(V4L2_PIX_FMT_NV16, RK_FORMAT_YCbCr_422_SP);
        m.emplace(V4L2_PIX_FMT_NV61, RK_FORMAT_YCrCb_422_SP);
        m.emplace(V4L2_PIX_FMT_YUYV, RK_FORMAT_YUYV_422);

        return m;
    }();
    return map;
}

inline const std::unordered_map<int, uint32_t>& rgaToV4l2FormatMap() {
    static const auto map = [] {
        std::unordered_map<int, uint32_t> m;
        m.reserve(14);
        m.max_load_factor(0.7f);

        // RGB
        m.emplace(RK_FORMAT_RGB_565, V4L2_PIX_FMT_RGB565);
        m.emplace(RK_FORMAT_RGB_888, V4L2_PIX_FMT_RGB24);
        m.emplace(RK_FORMAT_BGR_888, V4L2_PIX_FMT_BGR24);
        m.emplace(RK_FORMAT_ARGB_8888, V4L2_PIX_FMT_ARGB32);
        m.emplace(RK_FORMAT_ABGR_8888, V4L2_PIX_FMT_ABGR32);

        // YUV 4:2:0
        m.emplace(RK_FORMAT_YCbCr_420_SP, V4L2_PIX_FMT_NV12);
        m.emplace(RK_FORMAT_YCrCb_420_SP, V4L2_PIX_FMT_NV21);
        m.emplace(RK_FORMAT_YCbCr_420_P, V4L2_PIX_FMT_YUV420);
        m.emplace(RK_FORMAT_YCrCb_420_P, V4L2_PIX_FMT_YVU420);

        // YUV 4:2:2
        m.emplace(RK_FORMAT_YCbCr_422_SP, V4L2_PIX_FMT_NV16);
        m.emplace(RK_FORMAT_YCrCb_422_SP, V4L2_PIX_FMT_NV61);
        m.emplace(RK_FORMAT_YUYV_422, V4L2_PIX_FMT_YUYV);
        m.emplace(RK_FORMAT_UYVY_422, V4L2_PIX_FMT_UYVY);

        return m;
    }();
    return map;
}
} // namespace detail

/**
 * @brief DRM格式转RGA格式
 * @param drmFmt DRM格式(如DRM_FORMAT_NV12)
 * @return RGA格式, 未找到返回-1
 */
inline int convertDRMtoRGAFormat(uint32_t drmFmt) noexcept {
    const auto& map = detail::drmToRgaFormatMap();
    const auto it = map.find(drmFmt);
    if (it != map.end()) return it->second;
    return -1;
}

/**
 * @brief RGA格式转DRM格式
 * @param rgaFmt RGA格式(如RK_FORMAT_YCbCr_420_SP)
 * @return DRM格式, 未找到返回-1
 */
inline uint32_t convertRGAtoDrmFormat(int rgaFmt) noexcept {
    const auto& map = detail::rgaToDrmFormatMap();
    const auto it = map.find(rgaFmt);
    if (it != map.end()) return it->second;
    return static_cast<uint32_t>(-1);
}

/**
 * @brief V4L2格式转RGA格式
 * @param v4l2Fmt V4L2格式(如V4L2_PIX_FMT_NV12)
 * @return RGA格式, 未找到返回-1
 */
inline int convertV4L2toRGAFormat(uint32_t v4l2Fmt) noexcept {
    const auto& map = detail::v4l2ToRgaFormatMap();
    const auto it = map.find(v4l2Fmt);
    if (it != map.end()) return it->second;
    return -1;
}

/**
 * @brief RGA格式转V4L2格式
 * @param rgaFmt RGA格式(如RK_FORMAT_YCbCr_420_SP)
 * @return V4L2格式, 未找到返回-1
 */
inline uint32_t convertRGAtoV4L2Format(int rgaFmt) noexcept {
    const auto& map = detail::rgaToV4l2FormatMap();
    const auto it = map.find(rgaFmt);
    if (it != map.end()) return it->second;
    return static_cast<uint32_t>(-1);
}

/**
 * @brief 验证RGA格式是否有效
 * @param rgaFmt RGA格式
 * @return true如果格式有效, false否则
 */
inline bool isValidRgaFormat(int rgaFmt) noexcept {
    return convertRGAtoDrmFormat(rgaFmt) != static_cast<uint32_t>(-1);
}

/**
 * @brief 验证DRM格式是否有效
 * @param drmFmt DRM格式
 * @return true如果格式有效, false否则
 */
inline bool isValidDrmFormat(uint32_t drmFmt) noexcept {
    return convertDRMtoRGAFormat(drmFmt) != -1;
}

/**
 * @brief 验证V4L2格式是否有效
 * @param v4l2Fmt V4L2格式
 * @return true如果格式有效, false否则
 */
inline bool isValidV4l2Format(uint32_t v4l2Fmt) noexcept {
    return convertV4L2toRGAFormat(v4l2Fmt) != -1;
}

} // namespace rga
} // namespace utils

// 向后兼容: 保留旧的全局名字
using utils::rga::convertDRMtoRGAFormat;
using utils::rga::convertRGAtoDrmFormat;
using utils::rga::convertV4L2toRGAFormat;
using utils::rga::convertRGAtoV4L2Format;

// utils/mpp/formatTool.h
#pragma once
#include <rockchip/mpp_frame.h>
#include <drm/drm_fourcc.h>
#include <unordered_map>

inline MppFrameFormat drm2mpp_format(uint32_t drm_fmt) {
    static const std::unordered_map<uint32_t, MppFrameFormat> table = {
        { DRM_FORMAT_NV12,      MPP_FMT_YUV420SP },
        { DRM_FORMAT_NV21,      MPP_FMT_YUV420SP },
        { DRM_FORMAT_YUV420,    MPP_FMT_YUV420P  },
        { DRM_FORMAT_YVU420,    MPP_FMT_YUV420P  },
        { DRM_FORMAT_NV16,      MPP_FMT_YUV422SP },
        { DRM_FORMAT_NV61,      MPP_FMT_YUV422SP },
        { DRM_FORMAT_YUYV,      MPP_FMT_YUV422_YUYV },
        { DRM_FORMAT_UYVY,      MPP_FMT_YUV422_UYVY },
        { DRM_FORMAT_RGB565,    MPP_FMT_RGB565 },
        { DRM_FORMAT_BGR565,    MPP_FMT_BGR565 },
        { DRM_FORMAT_RGB888,    MPP_FMT_RGB888 },
        { DRM_FORMAT_BGR888,    MPP_FMT_BGR888 },
        { DRM_FORMAT_ARGB8888,  MPP_FMT_ARGB8888 },
        { DRM_FORMAT_ABGR8888,  MPP_FMT_ABGR8888 },
    };
    auto it = table.find(drm_fmt);
    return (it != table.end()) ? it->second : MPP_FMT_YUV420SP;
}

inline uint32_t mpp2drm_format(MppFrameFormat mpp_fmt) {
    static const std::unordered_map<MppFrameFormat, uint32_t> table = {
        { MPP_FMT_YUV420SP,     DRM_FORMAT_NV12 },
        { MPP_FMT_YUV420P,      DRM_FORMAT_YUV420 },
        { MPP_FMT_YUV422SP,     DRM_FORMAT_NV16 },
        { MPP_FMT_YUV422_YUYV,  DRM_FORMAT_YUYV },
        { MPP_FMT_YUV422_UYVY,  DRM_FORMAT_UYVY },
        { MPP_FMT_RGB565,       DRM_FORMAT_RGB565 },
        { MPP_FMT_BGR565,       DRM_FORMAT_BGR565 },
        { MPP_FMT_RGB888,       DRM_FORMAT_RGB888 },
        { MPP_FMT_BGR888,       DRM_FORMAT_BGR888 },
        { MPP_FMT_ARGB8888,     DRM_FORMAT_ARGB8888 },
        { MPP_FMT_ABGR8888,     DRM_FORMAT_ABGR8888 },
    };
    auto it = table.find(mpp_fmt);
    return (it != table.end()) ? it->second : DRM_FORMAT_NV12;
}
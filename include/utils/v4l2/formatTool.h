/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-12 13:53:10
 * @FilePath: /EdgeVision/include/utils/v4l2/formatTool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once 
#include <unordered_map>
#include <vector>
#include <linux/videodev2.h>

namespace FormatTool {
 
struct PlaneScale {
    float width_scale;
    float height_scale;
};

const static std::unordered_map<uint32_t, std::vector<PlaneScale>> formatPlaneMap = {
    // NV12/NV21: 也按双平面处理, 有的驱动只报告单平面
    { V4L2_PIX_FMT_NV12,  { {1.0f, 1.0f}, {1.0f, 0.5f} } },
    { V4L2_PIX_FMT_NV21,  { {1.0f, 1.0f}, {1.0f, 0.5f} } },
    { V4L2_PIX_FMT_NV16,  { {1.0f, 1.0f}, {1.0f, 1.0f} } },
    { V4L2_PIX_FMT_NV61,  { {1.0f, 1.0f}, {1.0f, 1.0f} } },
    { V4L2_PIX_FMT_NV24,  { {1.0f, 1.0f}, {1.0f, 1.0f} } },
    { V4L2_PIX_FMT_NV42,  { {1.0f, 1.0f}, {1.0f, 1.0f} } },

    // NV12M / NV21M
    { V4L2_PIX_FMT_NV12M, { {1.0f, 1.0f}, {1.0f, 0.5f} } },
    { V4L2_PIX_FMT_NV21M, { {1.0f, 1.0f}, {1.0f, 0.5f} } },

    // NV16M / NV61M
    { V4L2_PIX_FMT_NV16M, { {1.0f, 1.0f}, {1.0f, 1.0f} } },
    { V4L2_PIX_FMT_NV61M, { {1.0f, 1.0f}, {1.0f, 1.0f} } },

    // YUV420M / YVU420M
    { V4L2_PIX_FMT_YUV420M, { {1.0f, 1.0f}, {0.5f, 0.5f}, {0.5f, 0.5f} } },
    { V4L2_PIX_FMT_YVU420M, { {1.0f, 1.0f}, {0.5f, 0.5f}, {0.5f, 0.5f} } },

    // YUV422M / YVU422M
    { V4L2_PIX_FMT_YUV422M, { {1.0f, 1.0f}, {0.5f, 1.0f}, {0.5f, 1.0f} } },
    { V4L2_PIX_FMT_YVU422M, { {1.0f, 1.0f}, {0.5f, 1.0f}, {0.5f, 1.0f} } },

    // YUV444M
    { V4L2_PIX_FMT_YUV444M, { {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} } },
};

} // namespace formatTool
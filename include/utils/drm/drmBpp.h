#ifndef DRM_BPP_H
#define DRM_BPP_H

#include <linux/videodev2.h>

#include <xf86drm.h>            // DRM 核心功能
#include <xf86drmMode.h>        // DRM 模式设置功能
#include <drm/drm_fourcc.h>     // DRM 格式定义
#include <stdexcept>            // 需要抛异常
#include <string>               // 添加string支持
// 头文件内实现函数需要加上 inline避免多次展开出现重定义错误
// 对于头文件实现的类的成员函数,是隐式 inline 所以是安全的
inline uint32_t calculate_bpp(uint32_t format) {
    switch (format) {
        // 8-bit formats
        case DRM_FORMAT_R8:
        case DRM_FORMAT_C8:
            return 8;
        
        // 16-bit formats
        case DRM_FORMAT_RGB565:
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_NV16:
            return 16;
        
        // 24-bit formats
        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
            return 24;
        
        // 32-bit formats
        case DRM_FORMAT_ARGB8888:
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ABGR8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBA8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRA8888:
        case DRM_FORMAT_BGRX8888:
            return 32;
        
        // YUV formats (special handling)
        case DRM_FORMAT_NV12:
        case DRM_FORMAT_NV21:
            /*
             * 特殊说明：NV12实际内存占用为12bits/px
             * 但分配时需要两个独立平面：
             *   Plane0: Y分量 (width * height)
             *   Plane1: UV分量 (width * height / 2)
             */
            return 12;
        
        default:
            return -1;
    }
}

// v4l2 转换
inline uint32_t get_bpp(uint32_t pixelformat) {
    switch (pixelformat) {
        case V4L2_PIX_FMT_NV12:
            return calculate_bpp(DRM_FORMAT_NV12);

        case V4L2_PIX_FMT_NV16:
            return calculate_bpp(DRM_FORMAT_NV16);
        
        // 添加更多常用V4L2格式映射
        case V4L2_PIX_FMT_YUYV:
            return calculate_bpp(DRM_FORMAT_YUYV);
        
        case V4L2_PIX_FMT_RGB24:
            return calculate_bpp(DRM_FORMAT_RGB888);
        
        case V4L2_PIX_FMT_XRGB32:
            return calculate_bpp(DRM_FORMAT_XRGB8888);
        
        default:
            // 使用0作为错误标识更安全
            return -1;
    }
}

inline uint32_t convertV4L2ToDrmFormat(uint32_t v4l2_fmt) {
    switch (v4l2_fmt) {
        case V4L2_PIX_FMT_NV12:
            return DRM_FORMAT_NV12;
        case V4L2_PIX_FMT_NV16:
            return DRM_FORMAT_NV16;
        case V4L2_PIX_FMT_RGB24:
            return DRM_FORMAT_RGB888;
        case V4L2_PIX_FMT_YUYV:
            return DRM_FORMAT_YUYV;
        // 你可以继续添加更多格式
        default:
            return -1;
    }
}

#endif // DRM_BPP_H

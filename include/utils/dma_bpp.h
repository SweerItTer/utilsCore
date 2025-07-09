#ifndef DMA_BPP_H
#define DMA_BPP_H

#include <linux/videodev2.h>
#include <stdexcept> // 需要抛异常

inline uint32_t get_bpp(uint32_t pixelformat) {
    switch (pixelformat) {
        case V4L2_PIX_FMT_NV12:
            // NV12: YUV420 半平面格式, 理论 12bpp，取整数 16 方便 dumb buffer 对齐
            return 16;

        case V4L2_PIX_FMT_NV16:
            // NV16: YUV422 半平面格式, 理论 16bpp
            return 16;

        default:
            return -1;
    }
}

#endif // DMA_BPP_H

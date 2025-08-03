/*
 * @FilePath: /EdgeVision/include/utils/rga/rgaConverter.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-08 15:19:10
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef RGA_CONVERTER_H
#define RGA_CONVERTER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <memory.h>
#include <unistd.h>
#include <sys/time.h>

#include "rga/im2d.hpp"
#include "rga/RockchipRga.h"
#include "rga/RgaUtils.h"
#include "rga/RgaApi.h"
#include "rga/rga.h"

/* 对于 RK3568 平台以及大多数平台而言,多平面的 dma_buf 通常只需要 plane 0 的 fd
 * plane 1 多为"逻辑plane" 
 * 以 nv12 举个例子:
 *  Plane 0: Y    => 640 x 480 bytes
 *  Plane 1: UV   => 640 x 480 / 2 bytes
 * 物理布局通常是连续的
 * [ Y ... (307200 bytes) ][ UV ... (153600 bytes) ]
 * 
 * Q: 为什么是连续的?
 * A: 在至少rk3568上,v4l2 会在 VIDIOC_REQBUFS 时开辟了连续的内存,稍微的多平面
 * 其实是由实际的格式按顺序拆分为不同的 plane,实际上的 plane x多是通过 plane 0 和偏移值算出来的
 * 对于 dma_buf 句柄,哪怕不是物理连续的也可以通过 DMA映射表(IOMMU)正确访问
 * 并且在哲理上,V4L2和DMA-BUF就是抽象出来让开发者不用操心物理连续的库
 * 
 * 如果显式使用格式如: V4L2_PIX_FMT_NV12M, 则分配独立缓冲区,常用于GPU纹理渲染
 */
class RgaConverter {
public:
    /*  V4L2        RK 格式	
     *
     *  NV16        RK_FORMAT_YCbCr_422_SP
     *  NV61        RK_FORMAT_YCrCb_422_SP
     */
    struct RgaParams {
        rga_buffer_t &src;
        im_rect &src_rect;
        rga_buffer_t &dst;
        im_rect &dst_rect;
    };

    explicit RgaConverter ();
    ~RgaConverter ();

    /**
     * @brief 将NV16格式转换为RGBA8888
     * @param params 转换参数结构体
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     */
    IM_STATUS NV16toRGBA(RgaParams& params);
    
    /**
     * @brief 将NV12格式转换为RGBA8888
     * @param params 转换参数结构体
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     */
    IM_STATUS NV12toRGBA(RgaParams& params);

    IM_STATUS NV16toXRGB(RgaParams& params);

    IM_STATUS NV12toXRGB(RgaParams& params);

    
    // 禁用拷贝和赋值
    RgaConverter(const RgaConverter&) = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;
private:
    // RGA上下文
    RockchipRga m_rga;
    
    // 初始化标志
    bool m_initialized = false;
    
    /**
     * @brief 执行实际RGA转换操作
     * @param src_fmt 源格式 (RK_FORMAT_YCbCr_422_SP 或 RK_FORMAT_YCbCr_420_SP)
     * @param params 转换参数
     * @return IM_STATUS 转换状态
     * 使用 DMABUF 时 RGA 输出的是 DRM_FORMAT_RGBA8888 即从低位到高位是 [R][G][B][A] 排列
     * 实际 OpenGL 使用的是 [A][B][G][R] 的顺序,所以应该使用 DRM_FORMAT_ABGR8888
     */
    IM_STATUS convertImage(RgaSURF_FORMAT src_fmt, RgaSURF_FORMAT dst_fmt, RgaParams &params);
};

#endif // !RGA_CONVERTER_H
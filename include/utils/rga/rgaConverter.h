/*
 * @FilePath: /include/utils/rga/rgaConverter.h
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

    static RgaConverter& instance();

    // 禁用拷贝
    RgaConverter(const RgaConverter&) = delete;
    RgaConverter& operator=(const RgaConverter&) = delete;
    ~RgaConverter ();
    void deinit();

    /**
     * @brief 将源格式转换为目标格式
     * @param params 转换参数结构体
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     * @details
     * src_fmt 源格式 (RK_FORMAT_YCbCr_422_SP , RK_FORMAT_YCbCr_420_SP 等)
     * 使用 DMABUF 时 RGA 输出的是 DRM_FORMAT_RGBA8888 即从低位到高位是 [R][G][B][A] 排列
     * 实际 OpenGL 使用的是 [A][B][G][R] 的顺序,所以应该使用 DRM_FORMAT_ABGR8888
     */
    IM_STATUS FormatTransform(RgaParams& params);

    /**
     * @brief 将源图缩放为目标大小
     * @param params 转换参数结构体
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     * @details
     * 通过dst设置w和h以达到目的
     */
    IM_STATUS ImageResize(RgaParams& params);

    /**
     * @brief 在目标 buffer 的指定矩形区域内填充指定颜色
     *
     * @param dst_buffer 目标 RGA buffer (rga_buffer_t)
     * @param dst_rect   目标矩形区域 (im_rect), 填充的范围
     * @param color      填充颜色 (char), 颜色值含义依赖于目标 buffer 的像素格式
     *
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     * @details
     * 填充颜色的解释方式取决于 dst_buffer.format:
     *   - RGBA8888 格式下, color 通常代表一个 32 位值 (0xAARRGGBB)
     *   - YUV 格式下, color 通常只设置 Y 分量或由内部解释
     */
    IM_STATUS ImageFill(rga_buffer_t& dst_buffer, im_rect& dst_rect, char color);

    
    /**
     * @brief 将 src 指定矩形区域经过缩放、旋转等处理后写入 dst 指定矩形位置
     *
     * @param params  RgaParams 结构体
     * @param pat     可选的目标填充缓冲区(rga_buffer_t), 用于指定输出占位或背景, 默认空
     * @param prect   可选的裁剪矩形区域(im_rect), 对 dst 的输出区域进一步裁剪, 默认空
     * @param usage   可选使用标志, 控制 RGA buffer 的访问方式或缓存策略, 默认 0
     *
     * @return IM_STATUS 转换状态 (成功返回IM_STATUS_SUCCESS)
     * @details
     * 仅使用 dst 相关缓冲区信息来输出结果, src 缓冲区和矩形仅被读取
     * pat 与 prect 可用于复杂场景, 例如局部填充或 ROI 处理
     */
    IM_STATUS ImageProcess(RgaParams& params, rga_buffer_t pat = {}, im_rect prect = {}, int usage = 0);


private:
    explicit RgaConverter ();

    // RGA上下文
    RockchipRga m_rga;
    
    // 初始化标志
    bool m_initialized = false;
};

#endif // !RGA_CONVERTER_H
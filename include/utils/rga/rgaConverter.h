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

#include "errorinfo.h"

class RgaConverter {
public:
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
     */
    IM_STATUS convertImage(RgaSURF_FORMAT src_fmt, RgaParams& params);
};

#endif // !RGA_CONVERTER_H
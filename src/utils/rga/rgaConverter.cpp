/*
 * @FilePath: /EdgeVision/src/utils/rga/rgaConverter.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-04 19:43:27
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "rga/rgaConverter.h"

RgaConverter::RgaConverter() {
    // 初始化RGA上下文
    m_rga.RkRgaInit();
    fprintf(stdout, "%s", querystring(RGA_VERSION));
    m_initialized = true;
}

RgaConverter::~RgaConverter() {
    if (m_initialized) {
        // 清理RGA资源
        // m_rga.RkRgaDeInit();
    }
}

IM_STATUS RgaConverter::FormatTransform(RgaParams& params){
    return convertImage(params.src.format, params.dst.format, params);
}

IM_STATUS RgaConverter::convertImage(int src_fmt, int dst_fmt, RgaParams &params)
{
    if (false == m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }

    if (src_fmt != params.src.format) {
        return IM_STATUS_ILLEGAL_PARAM;
    }

    // 目标格式
    if (params.dst.format != dst_fmt) params.dst.format = dst_fmt;
    
    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (IM_STATUS_NOERROR != ret) {
        fprintf(stderr, "%s", imStrError(ret));
        return ret;
    }

    ret = imcvtcolor(params.src, params.dst, src_fmt, dst_fmt);
    if (IM_STATUS_SUCCESS != ret) {
        fprintf(stderr, "%s", imStrError(ret));
    }

    return ret;
}


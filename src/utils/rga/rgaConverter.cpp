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
    if (false == m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }

    if (params.dst.format == params.src.format) {
        return IM_STATUS_ILLEGAL_PARAM;
    }
    
    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (IM_STATUS_NOERROR != ret) {
        fprintf(stderr, "%s", imStrError(ret));
        return ret;
    }

    ret = imcvtcolor(params.src, params.dst, params.src.format, params.dst.format);
    if (IM_STATUS_SUCCESS != ret) {
        fprintf(stderr, "%s", imStrError(ret));
    }

    return ret;
}

IM_STATUS RgaConverter::ImageResize(RgaParams& params){
    if (!m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }
    if (params.dst.width == params.src.width && params.dst.height == params.src.height){
        return IM_STATUS_ILLEGAL_PARAM;
    }

    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "%s", imStrError(ret));
        return ret;
    }

    ret = imresize(params.src, params.dst);
    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "%s", imStrError(ret));
    }
    return ret;
}
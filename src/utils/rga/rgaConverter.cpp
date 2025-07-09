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
    m_initialized = true;
}

RgaConverter::~RgaConverter() {
    if (m_initialized) {
        // 清理RGA资源
        // m_rga.RkRgaDeInit();
    }
}

IM_STATUS RgaConverter::NV16toRGBA(RgaParams& params) {
    // { RK_FORMAT_YCbCr_422_SP,  "cbcr422sp" }  // 这是NV16
    // { RK_FORMAT_YCrCb_422_SP,  "crcb422sp" }  // 这是NV61
    fprintf(stdout, "Try to convert NV16 to RGBA\n");
    return convertImage(RK_FORMAT_YCbCr_422_SP, params);
}

IM_STATUS RgaConverter::NV12toRGBA(RgaParams& params) {
    fprintf(stdout, "Try to convert NV12 to RGBA\n");
    return convertImage(RK_FORMAT_YCbCr_420_SP, params);
}

IM_STATUS RgaConverter::convertImage(RgaSURF_FORMAT src_fmt, RgaParams &params)
{
    if (false == m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }

    if (src_fmt != params.src.format) {
        return IM_STATUS_ILLEGAL_PARAM;
    }

    // 强制写死目标格式
    params.dst.format = RK_FORMAT_RGBA_8888;

    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (IM_STATUS_NOERROR != ret) {
        fprintf(stderr, "%s", imStrError(ret));
        return ret;
    }

    ret = imcvtcolor(params.src, params.dst, params.src.format, RK_FORMAT_RGBA_8888);
    if (IM_STATUS_SUCCESS != ret) {
        fprintf(stderr, "%s", imStrError(ret));
    }

    return ret;
}


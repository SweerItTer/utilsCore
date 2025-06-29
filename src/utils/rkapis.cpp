#include "rkapis.h"
#include <iostream>

namespace rkapi {

RgaConverter::RgaConverter() {
    // 初始化RGA上下文
    m_rga.RkRgaInit();
    m_initialized = true;
}

RgaConverter::~RgaConverter() {
    if (m_initialized) {
        // 清理RGA资源
        m_rga.RkRgaDeInit();
    }
}

IM_STATUS RgaConverter::NV16toRGBA(RgaParams& params) {
    // { RK_FORMAT_YCbCr_422_SP,  "cbcr422sp" }  // 这是NV16
    // { RK_FORMAT_YCrCb_422_SP,  "crcb422sp" }  // 这是NV61
    return convertImage(RK_FORMAT_YCbCr_422_SP, params);
}

IM_STATUS RgaConverter::NV12toRGBA(RgaParams& params) {
    return convertImage(RK_FORMAT_YCbCr_420_SP, params);
}

IM_STATUS RgaConverter::convertImage(RgaSURF_FORMAT src_fmt, RgaParams& params) {
    if (!m_initialized) {
        // 未初始化
        return IM_STATUS_NOT_SUPPORTED;
    }
    if (src_fmt != params.src.format){
        // 非法参数取值
        return IM_STATUS_ILLEGAL_PARAM;
    }

    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (IM_STATUS_NOERROR != ret) {
        Error("%s",imStrError((IM_STATUS)ret));
        return ret;
    }

    ret = imcvtcolor(params.src, params.dst, params.src.format, params.dst.format);

    return ret;
}

} // namespace rkapi

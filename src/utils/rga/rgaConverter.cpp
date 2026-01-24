/*
 * @FilePath: /src/utils/rga/rgaConverter.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-04 19:43:27
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "rga/rgaConverter.h"

RgaConverter &RgaConverter::instance()
{
    static RgaConverter converter;
    return converter;
}

RgaConverter::RgaConverter() {
    // 初始化RGA上下文
    m_rga.RkRgaInit();
    fprintf(stdout, "%s\t", querystring(RGA_VERSION));
    m_initialized = true;
}

RgaConverter::~RgaConverter() {
    deinit();
}

void RgaConverter::deinit() {
    if (m_initialized) {
        m_rga.RkRgaDeInit(); 
        m_initialized = false;
    }
}

IM_STATUS RgaConverter::FormatTransform(RgaParams& params){
    if (false == m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }

    if (params.dst.format == params.src.format) {
        return IM_STATUS_ILLEGAL_PARAM;
    }
    
    // IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    // if (IM_STATUS_NOERROR != ret) {
    //     fprintf(stderr, "%s", imStrError(ret));
    //     return ret;
    // }

    IM_STATUS ret = imcvtcolor(params.src, params.dst, params.src.format, params.dst.format);
    if (IM_STATUS_SUCCESS != ret) {
        fprintf(stderr, "%s", imStrError(ret));
    }

    return ret;
}

IM_STATUS RgaConverter::ImageResize(RgaParams& params){
    if (!m_initialized) {
        return IM_STATUS_NOT_SUPPORTED;
    }
    
    IM_STATUS ret = imcheck(params.src, params.dst, params.src_rect, params.dst_rect);
    if (ret != IM_STATUS_NOERROR) {
        fprintf(stderr, "%s", imStrError(ret));
        return ret;
    }

    if (params.dst.width == params.src.width && params.dst.height == params.src.height){
        return imcopy(params.src, params.dst);
    }
    
    double scaleX = static_cast<double>(params.dst.width) / static_cast<double>(params.src.width);
    double scaleY = static_cast<double>(params.dst.height) / static_cast<double>(params.src.height);
    double scale  = (scaleX < scaleY) ? scaleX : scaleY;   // 取最小, 保证完整显示
    ret = imresize(params.src, params.dst, scale, scale, INTER_LINEAR, 1);
    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "%s", imStrError(ret));
    }
    return ret;
}

IM_STATUS RgaConverter::ImageFill(rga_buffer_t& dst_buffer, im_rect& dst_rect, char color){
    int imcolor = 0;
    for (int i = 0; i < 4; i++) {
        imcolor |= ((unsigned char)color << (i * 8));
    }
    
    // fprintf(stdout, "fill dst image (x y w h)=(%d %d %d %d) with color=0x%x\n",
    //     dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height, imcolor);
    IM_STATUS ret = imfill(dst_buffer, dst_rect, imcolor); // 填充指定区域为目标颜色

    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "%s\n", imStrError(ret));
    }
    return ret;
}

IM_STATUS RgaConverter::ImageProcess(RgaParams& params, rga_buffer_t pat, im_rect prect, int usage) {
    IM_STATUS ret = improcess(params.src, params.dst, pat, params.src_rect, params.dst_rect, prect, usage);
    if (ret <= 0) {
        fprintf(stderr, "%s\n", imStrError(ret));
    }
    return ret;
}

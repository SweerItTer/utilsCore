/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 23:46:28
 * @FilePath: /EdgeVision/include/model/preprocess.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include "m_types.h"
#include "dma/dmaBuffer.h"
#include "rga/rgaConverter.h"

namespace preprocess
{
// 计算缩放和填充
int convert_image_with_letterbox(const DmaBufferPtr& src, const DmaBufferPtr& dst, letterbox* letterbox, char color);

// 使用RGA预处理图像(缩放+填充)
int convert_image_rga(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color);
// 使用CPU预处理图像(缩放+填充)
int convert_image_cpu(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color);

} // namespace preprocess

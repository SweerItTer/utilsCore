/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 21:03:26
 * @FilePath: /EdgeVision/include/model/fileUtils.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <tuple>
#include <algorithm>

#include "m_types.h"
#include "dma/dmaBuffer.h"

// 读取模型文件内容
int read_data_from_file(const char *path, char **out_data);

// 读取图像数据到 DMABUF
DmaBufferPtr readImage(const std::string& image_path);
// 将DMABUF零拷贝到Mat
cv::Mat mapDmaBufferToMat(DmaBufferPtr img);
// 保存 DMABUF
void saveImage(const std::string &image_path, DmaBufferPtr dma_buf);
// 保存带识别结果的 DMABUF
int saveResultImage(DmaBufferPtr img, const object_detect_result_list& result_,
     const std::string& savePath = "./detected_result.jpg");
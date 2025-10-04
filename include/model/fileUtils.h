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

#include "dma/dmaBuffer.h"

// 读取模型文件内容
int read_data_from_file(const char *path, char **out_data);

// 读取图像数据到 DMABUF
DmaBufferPtr readImage(const std::string& image_path);

// 保存 DMABUF 到图像
int saveImage(const std::string& image_path, const DmaBufferPtr& dma_buf);

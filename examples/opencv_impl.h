/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-05 17:45:33
 * @FilePath: /EdgeVision/examples/opencv_impl.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <opencv2/opencv.hpp>
#include <algorithm>

/**
 * @brief 使用 OpenCV 实现的 Letterbox 预处理
 * @param src_mat 源图像
 * @param dst_w 目标宽度
 * @param dst_h 目标高度
 * @param color 填充颜色 (RGB 相同值)
 * @return 处理后的 cv::Mat
 */
cv::Mat opencv_letterbox(const cv::Mat& src_mat, int dst_w, int dst_h, char color) {
    int src_w = src_mat.cols;
    int src_h = src_mat.rows;

    // 1. 计算缩放比例 (等比例缩放)
    float scale = std::min(static_cast<float>(dst_w) / src_w, 
                           static_cast<float>(dst_h) / src_h);

    // 2. 计算缩放后的尺寸，并应用 RGA 类似的对齐限制 (可选)
    int resize_w = static_cast<int>(src_w * scale + 0.5f);
    int resize_h = static_cast<int>(src_h * scale + 0.5f);
    
    // 对齐到 RGA 要求 (width % 4, height % 2) 以保持行为完全一致
    int aligned_w = (resize_w / 4) * 4;
    int aligned_h = (resize_h / 2) * 2;

    // 3. 执行 OpenCV 缩放
    cv::Mat resized_img;
    cv::resize(src_mat, resized_img, cv::Size(aligned_w, aligned_h));

    // 4. 创建背景画布并填充颜色
    cv::Mat dst_mat(dst_h, dst_w, CV_8UC3, cv::Scalar(color, color, color));

    // 5. 计算居中偏移量 (Padding)
    int left_pad = (dst_w - aligned_w) / 2;
    int top_pad = (dst_h - aligned_h) / 2;

    // 6. 将缩放后的图像拷贝到画布中央 (ROI 操作)
    cv::Rect roi(left_pad, top_pad, aligned_w, aligned_h);
    resized_img.copyTo(dst_mat(roi));

    return dst_mat;
}
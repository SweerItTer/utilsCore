/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-03 21:25:48
 * @FilePath: /EdgeVision/src/model/fileUtils.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "fileUtils.h"

int read_data_from_file(const char *path, char **out_data)
{
    // 获取文件流
    FILE *fp = fopen(path, "rb");
    if(fp == NULL) {
        printf("fopen %s fail!\n", path);
        return -1;
    }
    // 获取文件大小
    fseek(fp, 0, SEEK_END);     // 将文件指针移动到文件末尾(无偏移)
    int file_size = ftell(fp);  // 从文件头到当前指针位置的字节数
    char *data = (char *)malloc(file_size+1);   // 申请足够空间并多一个字节存放'\0'
    data[file_size] = 0;        // 在最后添加'\0'
    // 读取文件内容
    fseek(fp, 0, SEEK_SET);     // 将文件指针重新移动到文件头(无偏移)
    if(file_size != fread(data, 1, file_size, fp)) {    // 读取文件内容到data,一次1字节
        // 如果读取的字节数和文件大小不一致,则读取失败
        printf("fread %s fail!\n", path);
        free(data);     // 释放内存
        fclose(fp);     // 关闭文件
        return -1;
    }
    // 关闭文件
    if(fp) {
        fclose(fp);
    }
    // 返回数据和文件大小
    *out_data = data;
    return file_size;
}

DmaBufferPtr readImage(const std::string &image_path) {
    cv::Mat rawimg = cv::imread(image_path);
    if (rawimg.empty()) {
        std::cerr << "Failed to read image: " << image_path << std::endl;
        exit(-1);
    }
    cv::Mat rgbimg;
    cv::cvtColor(rawimg, rgbimg, cv::COLOR_BGR2RGB);
    
    // 创建 DMABUF
    auto dma_buf = DmaBuffer::create(rgbimg.cols, rgbimg.rows, DRM_FORMAT_RGB888, 0);
    if (!dma_buf) {
        std::cerr << "Failed to create DmaBuffer" << std::endl;
        exit(-1);
    }
    
    // ==================== 关键修复：按行拷贝 ====================
    uint8_t* dst = (uint8_t*)dma_buf->map();
    uint8_t* src = rgbimg.data;
    
    int width = rgbimg.cols;
    int height = rgbimg.rows;
    int src_row_bytes = width * 3;  // OpenCV 行字节数（紧密）
    int dst_stride = dma_buf->pitch();  // DmaBuffer 行字节数（对齐）
    
    fprintf(stdout, "[readImage] Size: %dx%d\n", width, height);
    fprintf(stdout, "[readImage] OpenCV row: %d bytes, DmaBuffer stride: %d bytes\n", 
            src_row_bytes, dst_stride);
    
    // 逐行拷贝
    for (int y = 0; y < height; y++) {
        memcpy(dst + y * dst_stride, src + y * src_row_bytes, src_row_bytes);
    }
    
    dma_buf->unmap();
    
    fprintf(stdout, "[readImage] Successfully loaded image with stride alignment\n");
    return dma_buf;
}

// 辅助裁剪函数
static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

cv::Mat mapDmaBufferToMat(DmaBufferPtr img) {
    uint8_t* img_data = img->map();
    if (nullptr == img_data) {
        fprintf(stderr, "Failed to map image buffer\n");
        return {};
    }

    int img_w = img->width();
    int img_h = img->height();
    int img_pitch = img->pitch(); // 每行字节数
    const int channels = 3;

    // 注意：使用 img_pitch 作为 step，可以处理非连续行
    cv::Mat mat(img_h, img_w, CV_8UC3, img_data, img_pitch);

    // OpenCV 默认顺序是 BGR
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    // 返回时不要 unmap！否则 mat 指向的内存会失效
    // img->unmap(); // 不要在这里 unmap，除非确保 mat 不再使用
    return mat;
}

// 绘制检测结果并保存
int saveResultImage(DmaBufferPtr img, const object_detect_result_list& result_, const std::string& savePath) {
    cv::Mat finalMat = mapDmaBufferToMat(img);
    if (finalMat.empty()) return -1;

    const int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    const double fontScale = 0.6;
    const int thickness = 2;

    int img_w = finalMat.cols;
    int img_h = finalMat.rows;

    for (const auto& r : result_) {
        int x = clamp_int(static_cast<int>(std::round(r.box.x)), 0, img_w - 1);
        int y = clamp_int(static_cast<int>(std::round(r.box.y)), 0, img_h - 1);
        int w = static_cast<int>(std::round(r.box.w));
        int h = static_cast<int>(std::round(r.box.h));
        if (w <= 0 || h <= 0) continue;
        if (x + w > img_w) w = img_w - x;
        if (y + h > img_h) h = img_h - y;
        if (w <= 0 || h <= 0) continue;

        cv::Scalar color(
            50 + (r.class_id * 37) % 200,
            50 + (r.class_id * 91) % 200,
            50 + (r.class_id * 53) % 200
        );

        cv::Rect rect(x, y, w, h);
        cv::rectangle(finalMat, rect, color, 2);

        // 文本
        char text[128];
        snprintf(text, sizeof(text), "%s %.2f", r.class_name.c_str(), r.prop);

        int baseline = 0;
        cv::Size textSize = cv::getTextSize(text, fontFace, fontScale, thickness, &baseline);
        baseline += 2;
        int tx = x;
        int ty = y - baseline;
        if (ty - textSize.height < 0) {
            ty = y + textSize.height + baseline;
        }

        // 文本背景
        cv::rectangle(finalMat, cv::Point(tx, ty - textSize.height - baseline),
                      cv::Point(tx + textSize.width, ty + baseline/2), color, cv::FILLED);
        cv::putText(finalMat, text, cv::Point(tx, ty), fontFace, fontScale, cv::Scalar(255,255,255), thickness);
    }

    if (!cv::imwrite(savePath, finalMat)) {
        fprintf(stderr, "Failed to save %s\n", savePath.c_str());
        return -1;
    }

    fprintf(stdout, "Detection results saved to %s\n", savePath.c_str());
    return 0;
}

void saveImage(const std::string &image_path, DmaBufferPtr dma_buf){
    cv::Mat mat = mapDmaBufferToMat(dma_buf);
    cv::imwrite(image_path, mat);
}

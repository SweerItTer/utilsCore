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

int saveImage(const std::string &image_path, const DmaBufferPtr &dma_buf)
{
    if (nullptr == dma_buf) {
        std::cerr << "Invalid DmaBuffer pointer" << std::endl;
        return -1;
    }
    
    auto dumpDmabufAsRGB24 = [](int dmabuf_fd, uint32_t width, uint32_t height,
                                uint32_t size, uint32_t pitch, const char* path)
    {
        if (dmabuf_fd < 0 || width == 0 || height == 0 || size == 0 || pitch == 0) {
            fprintf(stderr, "[dump] Invalid argument\n");
            return false;
        }

        void* data = mmap(nullptr, size, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
        if (MAP_FAILED == data) {
            perror("mmap failed");
            return false;
        }

        FILE* fp = fopen(path, "wb");
        if (!fp) {
            perror("fopen failed");
            munmap(data, size);
            return false;
        }

        uint8_t* ptr = static_cast<uint8_t*>(data);
        
        // 调试信息
        printf("[dump] width=%d, height=%d, pitch=%d, size=%d\n", 
               width, height, pitch, size);
        
        // 根据 pitch 和 width 计算实际每行字节数
        uint32_t bytes_per_pixel = 3; // RGB888
        uint32_t expected_pitch = width * bytes_per_pixel;
        
        if (pitch == expected_pitch) {
            // 没有 padding，直接写入
            fwrite(data, 1, height * pitch, fp);
        } else {
            // 有 padding，需要逐行处理
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* row_start = ptr + y * pitch;
                fwrite(row_start, 1, expected_pitch, fp);
            }
        }

        fclose(fp);
        munmap(data, size);
        fprintf(stderr, "[dump] Saved %dx%d RGB24 raw image to %s\n", width, height, path);
        return true;
    };
    
    dumpDmabufAsRGB24(dma_buf->fd(), dma_buf->width(), dma_buf->height()
        , dma_buf->size(), dma_buf->pitch(), image_path.c_str());
    return 0;
}
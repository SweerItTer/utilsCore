/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-02 17:57:24
 * @FilePath: /EdgeVision/examples/rknn_test.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
/*
 * YOLOv5 RKNN 推理示例
 */
#include <iostream>
#include "yolov5.h"
#include "preprocess.h"
#include "rga/rga2drm.h"
#include "drm/deviceController.h"

// 问题根源
// OpenCV 的 cv::Mat 是紧密排列的数据（无填充），但 DmaBuffer 会按硬件要求对齐内存（每行有 padding）。直接 memcpy 整块数据导致内存布局不匹配，造成了 45 度倾斜和图像错位。
// 解决方案
// 在 readImage 函数中改为逐行拷贝，使用 dma_buf->pitch() 获取正确的行步长，这样数据就按照 DmaBuffer 期望的对齐方式存储了。
// 在 RGA 处理时，使用 pitch / 3 计算出正确的像素 stride（490），RGA 就能正确读取和缩放图像了。
int main() {
    DrmDev::fd_ptr = DeviceController::create();
    int ret = 0;
    rknn_app_context app_context{};
    std::string model_path = "/data/yolov5s_relu.rknn";
    std::string image0_path = "/data/bus.jpg";
    // std::string image1_path = "/data/bus.jpg";  // 你注释掉了这行，补上
    std::vector<DmaBufferPtr> Images;

    auto image0_buf = std::move(readImage(image0_path));
    // auto image1_buf = std::move(readImage(image1_path));
    if (nullptr == image0_buf ){//|| nullptr == image1_buf) {
        std::cerr << "Failed to read image\n";
        return -1;
    }
    Images.emplace_back(std::move(image0_buf));
    // Images.emplace_back(std::move(image1_buf));

    // 1. 加载模型
    ret = loadModel(model_path.c_str(), app_context);
    if (ret < 0) {
        std::cerr << "Load model failed.\n";
        return -1;
    }

    // 2. 加载IO信息
    ret = loadIOnum(app_context);
    if (ret < 0){
        std::cerr << "Load IO info failed.\n";
        return -1;
    }
    
    // 3. 设置输入mem
    ret = initializeMems(app_context, 2);
    if (ret < 0){
        std::cerr << "initialize Mems failed.\n";
        return -1;
    }

    // 准备输出缓冲区
    std::vector<rknn_output> outputs(app_context.io_num.n_output);
    for (int i = 0; i < app_context.io_num.n_output; i++) {
        outputs[i].want_float = 1;  // 获取浮点数输出
        outputs[i].is_prealloc = 0; // 让 RKNN 分配内存
    }

    int img_idx = 0;
    for (auto& img : Images){    
        fprintf(stdout, "\n========== Processing Image %d ==========\n", img_idx++);
        
        // 4. 图像预处理
        int bg_color = 114;
        letterbox letterbox_;
        auto mem = get_usable_mem(app_context.mem_pool);
        DmaBufferPtr dstbuf = DmaBuffer::importFromFD(mem->input_mems[0]->fd,
            app_context.model_width, app_context.model_height,
            formatRGAtoDRM(RK_FORMAT_RGB_888), mem->input_mems[0]->size);
        
        ret = convert_image_with_letterbox(img, dstbuf, &letterbox_, bg_color);
        if (ret < 0){
            std::cerr << "Pre process failed.\n";
            return -1;
        }
        fprintf(stdout, "[Preprocess] Success\n");
        
        saveImage("/data/output.rgb", dstbuf);  // 调试用
        
        ret = rknn_set_io_mem(app_context.rknn_ctx, mem->input_mems[0], &app_context.input_attrs[0]);
        if (ret < 0){
            std::cerr << "Set io mem failed.\n";
            return -1;
        }
        fprintf(stdout, "[SetIO] Success\n");
        
        // 5. 设置输出 mem
        for (int i = 0; i < app_context.io_num.n_output; i++) {
            ret = rknn_set_io_mem(app_context.rknn_ctx, mem->output_mems[i], 
                                  &app_context.output_attrs[i]);
            if (ret < 0) {
                fprintf(stderr, "Set output mem[%d] failed, ret=%d\n", i, ret);
                return -1;
            }
        }
        fprintf(stdout, "[SetOutputMem] Success\n");
        
        // 6. 运行推理
        ret = rknn_run(app_context.rknn_ctx, nullptr);
        if (ret < 0){
            fprintf(stderr, "rknn run failed, ret=%d\n", ret);
            return -1;
        }
        fprintf(stdout, "[Inference] Success\n");
        
        // 7. 读取输出数据
        fprintf(stdout, "\n[Output Verification]\n");
        for (int i = 0; i < app_context.io_num.n_output; i++) {
            fprintf(stdout, "Output[%d]: %s\n", i, app_context.output_attrs[i].name);
            fprintf(stdout, "  - Shape: [%d,%d,%d,%d]\n", 
                   app_context.output_attrs[i].dims[0],
                   app_context.output_attrs[i].dims[1],
                   app_context.output_attrs[i].dims[2],
                   app_context.output_attrs[i].dims[3]);
            fprintf(stdout, "  - Size: %u bytes\n", app_context.output_attrs[i].size);
            
            // 打印前几个值
            if ( mem->output_mems[i]->virt_addr) {
                int8_t* data = (int8_t*)mem->output_mems[i]->virt_addr;
                fprintf(stdout, "  - First 10 values: ");
                for (int j = 0; j < 10; j++) {
                    fprintf(stdout, "%d ", data[j]);
                }
                fprintf(stdout, "\n");
            }
        }
        
        fprintf(stdout, "========== Image %d Done ==========\n\n", img_idx - 1);
    }

    // 8. 释放资源    
    fprintf(stdout, "[Cleanup] Done\n");
    return 0;
}
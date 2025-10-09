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
#include "postprocess.h"
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
    std::string model_path = "./yolov5s_relu.rknn";
    std::string image0_path = "./bus.jpg";
    std::string image1_path = "./image.png";
    std::vector<DmaBufferPtr> Images;

    auto image0_buf = std::move(readImage(image0_path));
    auto image1_buf = std::move(readImage(image1_path));
    if (nullptr == image0_buf || nullptr == image1_buf) {
        std::cerr << "Failed to read image\n";
        return -1;
    }
    Images.emplace_back(std::move(image0_buf));
    Images.emplace_back(std::move(image1_buf));

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
        
        ret = preprocess::convert_image_with_letterbox(img, dstbuf, &letterbox_, bg_color);
        if (ret < 0){
            std::cerr << "Pre process failed.\n";
            return -1;
        }
        fprintf(stdout, "[Preprocess] Success\n");
        
        std::string prepath = "./preImage_" + std::to_string(img_idx) + ".jpg";
        saveImage(prepath, dstbuf);
        
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
        
        // 7. 后处理
        object_detect_result_list reslut_{};
        postprocess::read_class_names("./coco_80_labels_list.txt");
        postprocess::post_process_rule(app_context, mem->output_mems, letterbox_, reslut_);

        for (size_t i = 0; i < reslut_.size(); ++i) {
            const auto& r = reslut_[i];
            const auto& box = r.box;
            float prop = r.prop;
            const std::string& name = r.class_name;

            printf("Result %zu: box=[%.2f, %.2f, %.2f, %.2f], prop=%.3f, class=%s\n",
                i, box.x, box.y, box.w, box.h, prop, name.c_str());
        }

        // 8. 绘制
        std::string postpath = "./detected_result_" + std::to_string(img_idx) + ".jpg";
        saveResultImage(img, reslut_, postpath);
        
        fprintf(stdout, "========== Image %d Done ==========\n\n", img_idx - 1);
    }

    // 8. 释放资源
    return 0;
}
/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-13 22:00:32
 * @FilePath: /EdgeVision/src/model/yolov5s.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "yolov5s.h"
#include "rga/rga2drm.h"
#include <iostream>

Yolov5s::Yolov5s(const std::string &modelPath, const std::string &COCOPath,
                 float NMS_THRESH, float BOX_THRESH, AnchorSet anchorSet)
    : modelPath_(modelPath), confThres(BOX_THRESH), iouThresh(NMS_THRESH), anchorSet_(anchorSet){
    postprocess::read_class_names(COCOPath, classes);
}

int Yolov5s::init(rknn_app_context& inCtx, bool isChild) {
    int ret = 0;
    // 1. 加载模型
    if (true == isChild){
        ret = rknn_dup_context(&inCtx.rknn_ctx, &appCtx.rknn_ctx);
    } else {
        ret = loadModel(modelPath_.c_str(), appCtx);
        if (ret < 0) {
            fprintf(stderr, "Load model failed.\n");
            return -1;
        }
    }
    
    // 2. 加载IO信息
    ret = loadIOnum(appCtx);
    if (ret < 0){
        fprintf(stderr, "Load IO info failed.\n");
        return -1;
    }
    
    // 3. 设置输入mem
    ret = initializeMems(appCtx);
    if (ret < 0){
        fprintf(stderr, "initialize Mems failed.\n");
        return -1;
    }
    return ret;
}

object_detect_result_list Yolov5s::infer(DmaBufferPtr in_dmabuf)
{
    // 4. 图像预处理
    int bg_color = 114;
    letterbox letterbox_;
    rknn_io_tensor_mem* mem = &appCtx.io_mem;
    DmaBufferPtr dstbuf = DmaBuffer::importFromFD(mem->input_mems[0]->fd,
        appCtx.model_width, appCtx.model_height,
        formatRGAtoDRM(RK_FORMAT_RGB_888), mem->input_mems[0]->size);
    // 将 in_dmabuf 预处理并输出到 dstbuf
    int ret = preprocess::convert_image_with_letterbox(in_dmabuf, dstbuf, &letterbox_, bg_color);
    if (ret < 0){
        fprintf(stderr, "Pre process failed.\n");
        return {};
    }
    // fprintf(stdout, "[Preprocess] Success\n");

    // 5.1 设置输入 mem (包含 dstbuf)
    ret = rknn_set_io_mem(appCtx.rknn_ctx, mem->input_mems[0], &appCtx.input_attrs[0]);
    if (ret < 0){
        fprintf(stderr, "Set io mem failed.\n");
        return {};
    }
    // fprintf(stdout, "[SetIO] Success\n");

    // 5.2 设置输出 mem
    for (int i = 0; i < appCtx.io_num.n_output; i++) {
        ret = rknn_set_io_mem(appCtx.rknn_ctx, mem->output_mems[i], 
                                &appCtx.output_attrs[i]);
        if (ret < 0) {
            fprintf(stderr, "Set output mem[%d] failed, ret=%d\n", i, ret);
            return {};
        }
    }
    // fprintf(stdout, "[SetOutputMem] Success\n");

    // 6. 运行推理
    ret = rknn_run(appCtx.rknn_ctx, nullptr);
    if (ret < 0){
        fprintf(stderr, "rknn run failed, ret=%d\n", ret);
        return {};
    }
    // fprintf(stdout, "[Inference] Success\n")
    
    // 7. 后处理
    object_detect_result_list reslut_{};
    ret = postprocess::post_process_rule( 
        appCtx, mem->output_mems,
        letterbox_, classes, reslut_,
        confThres, iouThresh, anchorSet_);
    if (ret < 0){
        fprintf(stderr, "Post process failed, ret=%d\n", ret);
        return {};
    }
    // fprintf(stdout, "[Inference] Success\n");
    return reslut_;
}

int Yolov5s::drawBox(const object_detect_result_list &results, DmaBufferPtr outBuf, bool drawText)
{
    cv::Mat finalMat = mapDmaBufferToMat(outBuf);
    if (finalMat.empty()){
        fprintf(stderr, "[OpenCV] Failed to map dmabuf...\n");
        return -1;
    }
    cv::cvtColor(finalMat, finalMat, cv::COLOR_RGB2BGR);
    // fprintf(stdout, "[OpenCV] Drawing...\n");
    const int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    const double fontScale = 0.6;
    const int thickness = 2;

    int img_w = finalMat.cols;
    int img_h = finalMat.rows;

    for (const auto& r : results) {
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

        if (!drawText) continue;
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
    outBuf->unmap();
    finalMat.release();
    return 0;
}

DmaBufferPtr Yolov5s::infer(DmaBufferPtr in_dmabuf, bool drawText)
{
    // 获取后处理结果
    auto reslut_ = infer(in_dmabuf);
    if (reslut_.empty()){
        // fprintf(stderr, "Empty reslut.\n");
        return in_dmabuf;
    }
    // 绘制
    int ret = drawBox(reslut_, in_dmabuf, drawText);
    if (ret < 0){
        fprintf(stderr, "draw boxs failed, ret=%d\n", ret);
        return nullptr;
    }    
    return in_dmabuf;
}

rknn_app_context &Yolov5s::getCurrentContext() { return appCtx; }
#include "preprocess.h"

int preprocess::convert_image_rga(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color) {
    int src_w = src->width();
    int src_h = src->height();
    int src_pitch = src->pitch();

    int dst_w = dst->width();
    int dst_h = dst->height();
    int dst_pitch = dst->pitch();
    // 输出信息
    // fprintf(stdout, "[RGA] Source: %dx%d, pitch=%d\n", src_w, src_h, src_pitch);
    // fprintf(stdout, "[RGA] Dest: %dx%d, pitch=%d\n", dst_w, dst_h, dst_pitch);
    
    if (dst_box->right >= dst_w || dst_box->bottom >= dst_h) {
        fprintf(stderr, "[RGA ERROR] Destination box out of bounds!\n");
        return -1;
    }
    
    IM_STATUS ret = IM_STATUS_NOERROR;
    im_rect src_rect{};
    im_rect dst_rect{};
    im_rect prect{};
    rga_buffer_t pat{};
    int usage = 0;
    /* 对齐到 4 是 RGA 处理区域尺寸的要求(输入输出的宽高要求)
     * pitch / 3 是计算内存行步长, 告诉 RGA 内存中每行实际占多少像素宽度
     */
    // pitch = 1472 字节 RGB -> 3字节/像素 --> stride (像素) = pintch / 3
    // 使用 pitch 计算真实的 stride ()
    int src_wstride = src_pitch / src->channel();  // 1472 / 3 = 490
    int dst_wstride = dst_pitch / 3;  // 1920 / 3 = 640
    int src_hstride = src_h;
    int dst_hstride = dst_h;
    
    // fprintf(stdout, "[RGA] Stride (pixels): src=%dx%d, dst=%dx%d\n", 
    //         src_wstride, src_hstride, dst_wstride, dst_hstride);
    
    // 包装 buffer
    rga_buffer_t src_rgabuf = wrapbuffer_fd(
        src->fd(), src_w, src_h, RK_FORMAT_RGBA_8888,
        src_wstride, src_hstride);
        
    rga_buffer_t dst_rgabuf = wrapbuffer_fd(
        dst->fd(), dst_w, dst_h, RK_FORMAT_RGB_888,
        dst_wstride, dst_hstride);

    int src_region_w = src_box->right - src_box->left + 1;
    int src_region_h = src_box->bottom - src_box->top + 1;
    int dst_region_w = dst_box->right - dst_box->left + 1;
    int dst_region_h = dst_box->bottom - dst_box->top + 1;

    src_rect.x = src_box->left;
    src_rect.y = src_box->top;
    src_rect.width = src_region_w;
    src_rect.height = src_region_h;

    dst_rect.x = dst_box->left;
    dst_rect.y = dst_box->top;
    dst_rect.width = dst_region_w;
    dst_rect.height = dst_region_h;

    RgaConverter::RgaParams parame{
        .src = src_rgabuf,
        .src_rect = src_rect,
        .dst = dst_rgabuf,
        .dst_rect = dst_rect
    };

    // 填充
    im_rect whole_dst_rect = {0, 0, dst_w, dst_h};
    ret = RgaConverter::instance().ImageFill(parame.dst, whole_dst_rect, color);
    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "[RGA ERROR] ImageFill failed, STATUS=%d\n", ret);
        return -1;
    }
    // 缩放
    ret = RgaConverter::instance().ImageProcess(parame, pat, prect, usage);
    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "[RGA ERROR] ImageProcess failed, STATUS=%d\n", ret);
        return -1;
    }
    
    // fprintf(stdout, "[RGA] Success\n");
    return static_cast<int>(ret);
}

int convert_image(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color) {
    return preprocess::convert_image_rga(src, dst, src_box, dst_box, color);
}

int preprocess::convert_image_with_letterbox(const DmaBufferPtr& src, const DmaBufferPtr& dst, letterbox* letterbox, char color) {
    if(nullptr == src || nullptr == dst){
        fprintf(stderr, "[Letterbox] Invalid src or dst buffer\n");
        return -1;
    }

    int src_w = src->width();
    int src_h = src->height();
    int dst_w = dst->width();
    int dst_h = dst->height();

    // fprintf(stdout, "[Letterbox] Source: %dx%d, Target: %dx%d\n", src_w, src_h, dst_w, dst_h);

    // 1. 计算缩放比例
    float scale_w = static_cast<float>(dst_w) / src_w;
    float scale_h = static_cast<float>(dst_h) / src_h;
    float scale = std::min(scale_w, scale_h);

    // 2. 计算缩放后的尺寸
    int resize_w = static_cast<int>(src_w * scale + 0.5f);
    int resize_h = static_cast<int>(src_h * scale + 0.5f);

    // 3. 对齐到 RGA 要求 (width %4, height %2)
    int aligned_w = (resize_w / 4) * 4;
    int aligned_h = (resize_h / 2) * 2;
    
    if(aligned_w > dst_w) aligned_w -= 4;
    if(aligned_h > dst_h) aligned_h -= 2;
    
    if(aligned_w <= 0 || aligned_h <= 0) {
        fprintf(stderr, "[Letterbox] Invalid aligned size: %dx%d\n", aligned_w, aligned_h);
        return -1;
    }

    // 4. 计算填充
    int total_pad_w = dst_w - aligned_w;
    int total_pad_h = dst_h - aligned_h;

    int left_pad = total_pad_w / 2;
    int right_pad = total_pad_w - left_pad;
    int top_pad = total_pad_h / 2;
    int bottom_pad = total_pad_h - top_pad;

    // fprintf(stdout, "[Letterbox] Resized: %dx%d -> Aligned: %dx%d\n", 
    //         resize_w, resize_h, aligned_w, aligned_h);
    // fprintf(stdout, "[Letterbox] Padding - Left: %d, Right: %d, Top: %d, Bottom: %d\n",
    //         left_pad, right_pad, top_pad, bottom_pad);

    // 5. 保存 letterbox 信息
    if(letterbox){
        letterbox->scale = scale;
        letterbox->x_pad = left_pad;
        letterbox->y_pad = top_pad;
    }

    // 6. 设置源和目标区域
    rect src_box = {0, 0, src_w - 1, src_h - 1};
    rect dst_box = {
        left_pad,
        top_pad,
        left_pad + aligned_w - 1,
        top_pad + aligned_h - 1
    };

    // fprintf(stdout, "[Letterbox] src_box: left=%d, top=%d, right=%d, bottom=%d\n",
    //         src_box.left, src_box.top, src_box.right, src_box.bottom);
    // fprintf(stdout, "[Letterbox] dst_box: left=%d, top=%d, right=%d, bottom=%d\n",
    //         dst_box.left, dst_box.top, dst_box.right, dst_box.bottom);

    // 7. 执行转换
    int ret = convert_image(src, dst, &src_box, &dst_box, color);
    if(ret < 0){
        fprintf(stderr, "[Letterbox] convert_image failed\n");
        return -1;
    }

    // fprintf(stdout, "[Letterbox] Success - Image centered at (%d,%d) with size %dx%d\n",
    //         left_pad, top_pad, aligned_w, aligned_h);

    return 0;
}
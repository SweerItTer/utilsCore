#include "rga/rgaConverter.h"
#include "v4l2/camera_controller.h"
#include "safeQueue.h"

int main(int argc, char const *argv[])
{
    // 创建队列
    SafeQueue<Frame> queue(10);

    // 创建 RGA 转换器
    RgaConverter converter;

    // 相机配置
    CameraController::Config cfg = {
        .buffer_count = 4,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        // .width = 2560,
        // .height = 1440,
        .width = 3840,
        .height = 2160,
        .format = V4L2_PIX_FMT_NV12
    };
    

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
        RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;

    // 初始化相机控制器
    CameraController cctr(cfg);
    /* 捕获了 queue 的引用 
     * 闭包对象内部只保存了一个指向 queue 的引用
     * std::move 只移动了 queue 的引用,所有权没变
     * 并且避免拷贝, queue 禁用拷贝
     */
    cctr.setFrameCallback([&queue](Frame f) {
        queue.enqueue(std::move(f));  // 将 f 转成右值引用
    });
    cctr.start();

    // 取出一帧
    Frame frame(nullptr,0,0,-1);
    while (!queue.try_dequeue(frame));
    cctr.pause();

    // dmabuf_fd 由 V4L2 提供
    rga_buffer_t src;
    memset(&src, 0, sizeof(src));
    src.fd = frame.dmabuf_fd();
    src.width = cfg.width;
    src.height = cfg.height;
    src.wstride = cfg.width;
    src.hstride = cfg.height;
    src.format = format;

    // 目标 buffer（需要申请新 fd 或分配内存，这里举例分配 virAddr）
    int dst_size = cfg.width * cfg.height * 4; // RGBA8888 一般按4字节算
    void* dst_data = malloc(dst_size);
    if (nullptr == dst_data) {
        fprintf(stderr, "Failed to allocate dst buffer");
        return -1;
    }

    rga_buffer_t dst;
    memset(&dst, 0, sizeof(dst));
    dst.vir_addr = dst_data;
    dst.width = cfg.width;
    dst.height = cfg.height;
    dst.wstride = cfg.width;
    dst.hstride = cfg.height;

    // 源、目标矩形区域（默认全部图像）
    im_rect src_rect = {0, 0, cfg.width, cfg.height};
    im_rect dst_rect = {0, 0, cfg.width, cfg.height};

    // 参数
    RgaConverter::RgaParams rgaP {
        .src = src,
        .src_rect = src_rect,
        .dst = dst,
        .dst_rect = dst_rect
    };

    
    // 转换
    IM_STATUS status;
    if (RK_FORMAT_YCbCr_420_SP == format){
        status = converter.NV12toRGBA(rgaP);
    } else {
        status = converter.NV16toRGBA(rgaP);
    }
    if (IM_STATUS_SUCCESS != status) {
        printf("1");
        free(dst_data);
        return -1;
    }
    
    // 保存为图像文件
    FILE* fp = fopen("output.rgba", "wb");
    if (nullptr == fp) {
        fprintf(stderr, "Failed to open output file");
        free(dst_data);
        // 转换后入队buffer
        cctr.returnBuffer(frame.index());
        // 停止相机
        cctr.stop();
        return -1;
    }
    fwrite(dst_data, 1, dst_size, fp);
    fclose(fp);

    // 释放内存
    free(dst_data);
    // 转换后入队buffer
    cctr.returnBuffer(frame.index());
    // 停止相机
    cctr.stop();
    return 0;
}

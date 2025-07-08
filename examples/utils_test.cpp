#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rga/rgaConverter.h"
#include "v4l2/camera_controller.h"
#include "safeQueue.h"

int main(int argc, char const *argv[])
{
    using frameQueue = SafeQueue<Frame>;
    // 创建队列
    frameQueue queue(10);

    // 创建 RGA 转换器
    RgaConverter converter;

    // 相机配置
    CameraController::Config cfg = {
        .use_dmabuf = true,
        .plane_count = 2,
        .width = 640,
        .height = 480,
        .format = V4L2_PIX_FMT_NV12
    };

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ? RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCbCr_422_SP;

    // 初始化相机控制器
    CameraController cctr(cfg);
    /* 捕获了 queue 的引用 
     * 闭包对象内部只保存了一个指向 queue 的引用
     * std::move 只移动了 queue 的引用,所有权没变
     * 并且避免拷贝, queue 禁用拷贝
     */
    cctr.setFrameCallback([&queue](Frame f) { queue.enqueue(f); });
    cctr.start();

    // 取出一帧
    Frame frame = queue.dequeue();

    // 源 buffer（dmabuf_fd 已经由 V4L2 提供）
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
        std::cerr << "Failed to allocate dst buffer" << std::endl;
        return -1;
    }

    rga_buffer_t dst;
    memset(&dst, 0, sizeof(dst));
    dst.virAddr = dst_data;
    dst.width = cfg.width;
    dst.height = cfg.height;
    dst.wstride = cfg.width;
    dst.hstride = cfg.height;
    dst.format = RK_FORMAT_RGBA_8888;

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
    IM_STATUS status = converter.NV12toRGBA(rgaP);
    if (IM_STATUS_SUCCESS != status) {
        std::cerr << "RGA convert failed: " << imStrError(status) << std::endl;
        free(dst_data);
        return -1;
    }

    // 保存为图像文件
    FILE* fp = fopen("output.png", "wb");
    if (nullptr == fp) {
        std::cerr << "Failed to open output file" << std::endl;
        free(dst_data);
        return -1;
    }
    fwrite(dst_data, 1, dst_size, fp);
    fclose(fp);

    // 释放内存
    free(dst_data);

    // 停止相机
    cctr.stop();

    return 0;
}

#include "rga/rgaProcessor.h"
#include "v4l2/cameraController.h"
#include "dma/dmaBuffer.h"
#include "safeQueue.h"

#include <iostream>
#include <thread>

int rgaTest();
int dmabufTest();

int main(int argc, char const *argv[]) {
    // 资源初始化
    DmaBuffer::initialize_drm_fd();
    
    int ret = 0;
    // 参数定义
    const char* rgatest_opt = "--rgatest";
    const char* dmatest_opt = "--dmatest";
    const char* help_opt = "--help";
    
    // 参数解析
    bool rgatest_flag = false;
    bool dmatest_flag = false;
    bool help_flag = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], rgatest_opt) == 0) {
            rgatest_flag = true;
        } else if (strcmp(argv[i], dmatest_opt) == 0) {
            dmatest_flag = true;
        } else if (strcmp(argv[i], help_opt) == 0) {
            help_flag = true;
        } else {
            std::cerr << "未知选项: " << argv[i] << std::endl;
            return 1;
        }
    }

    // 显示帮助信息
    if (help_flag || argc == 1) {
        std::cout << "用法: " << argv[0] << " [选项]" << std::endl;
        std::cout << "选项:" << std::endl;
        std::cout << "  --rgatest   运行RGA测试" << std::endl;
        std::cout << "  --dmatest   运行DMABUF测试" << std::endl;
        std::cout << "  --help     显示此帮助信息" << std::endl;
        return 0;
    }

    // 验证互斥选项
    if (rgatest_flag && dmatest_flag) {
        std::cerr << "错误: 不能同时指定--rgatest和--dmatest" << std::endl;
        return 1;
    }

    try {
        if (rgatest_flag) {
            ret = rgaTest();
        } else if (dmatest_flag) {
            ret = dmabufTest();
        } else {
            std::cerr << "未指定任何测试选项" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "运行时错误: " << e.what() << std::endl;
        ret = 1;
    } catch (...) {
        std::cerr << "未知错误发生" << std::endl;
        ret = 1;
    }
    // 资源清理
    DmaBuffer::close_drm_fd();
    return ret;
}

int virSave(void *data, size_t buffer_size){
    // 保存为图像文件
    FILE* fp = fopen("output.rgba", "wb");
    if (nullptr == fp) {
        fprintf(stderr, "Failed to open output file");
        free(data);
        return -1;
    }
    fwrite(data, 1, buffer_size, fp);
    fclose(fp);

    // 释放内存
    free(data);
     
    return 0;
}

int rgaTest(){
    // 创建队列
    auto rawFrameQueue  	= std::make_shared<FrameQueue>(4);
    auto frameQueue     	= std::make_shared<FrameQueue>(4);

    // 相机配置
    CameraController::Config cfg = {
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = 1920,
        .height = 1080,
        // .width = 3840,
        // .height = 2160,
        .format = V4L2_PIX_FMT_NV12
    };
    
    // 初始化相机控制器
    auto cctr         	= std::make_shared<CameraController>(cfg);

    cctr->setFrameCallback([rawFrameQueue](Frame f) {
        rawFrameQueue->enqueue(std::move(f));
    });

    RgaConverter converter_ ;

    Frame::MemoryType frameType = (true == cfg.use_dmabuf)
    ? Frame::MemoryType::DMABUF
    : Frame::MemoryType::MMAP;

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
        RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;

    cctr->start();
    // 源图像
    rga_buffer_t src {};
    // 输出图像
    rga_buffer_t dst {};

    src.width = cfg.width;
    src.height = cfg.height;
    src.wstride = cfg.width;
    src.hstride = cfg.height;
    src.format = format;
    
    dst = src;
    
    auto frame = rawFrameQueue->dequeue();
    cctr->pause();
    src.fd = frame.dmabuf_fd();

    auto bufptr =  DmaBuffer::create(cfg.width, cfg.height, DRM_FORMAT_RGBA8888);

    dst.fd = bufptr->fd();
    
    dst.format = RK_FORMAT_RGBA_8888;

    im_rect rect = {0, 0, static_cast<int>(cfg.width), static_cast<int>(cfg.height)};
    RgaConverter::RgaParams params {src, rect, dst, rect};
    // 格式转换
    IM_STATUS status = converter_.FormatTransform(params);
    
    // rawQueue_ 需要 returnBuffer, 需要传递 CameraController
    // 不管是否转换成功都归还
    cctr->returnBuffer(frame.index());
    
    if (IM_STATUS_SUCCESS != status) {
        fprintf(stderr, "RGA convert failed: %d\n", status);
    }
    RgaProcessor::dumpDmabufAsRGBA(bufptr->fd(), bufptr->width(), bufptr->height(), bufptr->size(), bufptr->pitch(), "./end.rgba");

    // 停止相机
    cctr->stop();
    
    return 0;
}

int dmabufTest() 
{   
    SafeQueue<DmaBufferPtr> queue_(8);

    for (int i = 0; i < 8; ++i)
    {
        DmaBufferPtr buf = DmaBuffer::create(1920, 1080, DRM_FORMAT_XRGB8888);
        queue_.enqueue(std::move(buf));
    }
    auto size = queue_.size();
    for (int i = 0; i < size; i++){
        auto buf = queue_.dequeue();
        std::cout << "Prime fd: " << buf->fd() << ", Size: " << buf->size()
            << ", Width: " << buf->width() << ", Height: " << buf->height() << std::endl;
    }
    return 0;
}

/*root@ATK-DLRK3568:~/EdgeVision/examples# ./utils_test --rgatest
rga_api version 1.3.1_[11] (RGA is compiling with meson base: $PRODUCT_BASE)
Warning: driver changed resolution to 3840x2160
Had init the rga dev ctx = 0xb9ae4d0
rga_api version 1.3.1_[11] (RGA is compiling with meson base: $PRODUCT_BASE)
Had init the rga dev ctx = 0xb9ae4d0
rga_api version 1.3.1_[11] (RGA is compiling with meson base: $PRODUCT_BASE)
Closing plane dmabuf fd 6
DmaBuffer::cleanup(): fd=6, handle=1
Closing plane dmabuf fd 7
DmaBuffer::cleanup(): fd=7, handle=2
Closing plane dmabuf fd 8
DmaBuffer::cleanup(): fd=8, handle=3
Closing plane dmabuf fd 9
DmaBuffer::cleanup(): fd=9, handle=4
close fd: 5
close fd: 3
root@ATK-DLRK3568:~/EdgeVision/examples# ^C
root@ATK-DLRK3568:~/EdgeVision/examples# ./utils_test --rgatest
Warning: driver changed resolution to 3840x2160
rga_api version 1.3.1_[11] (RGA is compiling with meson base: $PRODUCT_BASE)
Had init the rga dev ctx = 0x3a4998c0
rga_api version 1.3.1_[11] (RGA is compiling with meson base: $PRODUCT_BASE)
[dump] Saved 3840x2160 RGBA8888 image to /end.rgba
Closing plane dmabuf fd 5
DmaBuffer::cleanup(): fd=5, handle=1
Closing plane dmabuf fd 6
DmaBuffer::cleanup(): fd=6, handle=2
Closing plane dmabuf fd 7
DmaBuffer::cleanup(): fd=7, handle=3
Closing plane dmabuf fd 8
DmaBuffer::cleanup(): fd=8, handle=4
DmaBuffer::cleanup(): fd=10, handle=5
close fd: 4
close fd: 3*/
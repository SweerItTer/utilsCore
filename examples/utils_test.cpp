#include "rga/rgaConverter.h"
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
    // 暂停采集线程
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

    auto bufptr =  DmaBuffer::create(cfg.width, cfg.height, DRM_FORMAT_RGBA8888);

    rga_buffer_t dst;
    memset(&dst, 0, sizeof(dst));
    dst.vir_addr = dst_data;
    dst.width = cfg.width;
    dst.height = cfg.height;
    dst.wstride = cfg.width;
    dst.hstride = cfg.height;

    // 源、目标矩形区域（默认全部图像）
    im_rect rect = {0, 0, static_cast<int>(cfg.width), static_cast<int>(cfg.height)};

    // 参数
    RgaConverter::RgaParams rgaP { src, rect, dst, rect};

    
    // 转换
    IM_STATUS status;
    if (RK_FORMAT_YCbCr_420_SP == format){
        status = converter.NV12toRGBA(rgaP);
    } else {
        status = converter.NV16toRGBA(rgaP);
    }
    if (IM_STATUS_SUCCESS != status) {
        fprintf(stderr, "RGA convert failed: %d\n", status);
    }
    // 停止相机
    cctr.stop();
    // // 保存为图像文件
    // FILE* fp = fopen("output.rgba", "wb");
    // if (nullptr == fp) {
    //     fprintf(stderr, "Failed to open output file");
    //     free(dst_data);
    //     // 转换后入队buffer
    //     cctr.returnBuffer(frame.index());
    //     // 停止相机
    //     cctr.stop();
    //     return -1;
    // }
    // fwrite(dst_data, 1, dst_size, fp);
    // fclose(fp);

    // // 释放内存
    // free(dst_data);
    
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


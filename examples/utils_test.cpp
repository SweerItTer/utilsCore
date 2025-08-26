#include "rga/rgaProcessor.h"
#include "v4l2/cameraController.h"
#include "dma/dmaBuffer.h"
#include "safeQueue.h"

#include <iostream>
#include <thread>

// 资源初始化

int rgaTest();
int dmabufTest();

int main(int argc, char const *argv[]) {
    DrmDev::fd_ptr = DeviceController::create();
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
    auto rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    auto frameQueue     	= std::make_shared<FrameQueue>(10);

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

    cctr->setFrameCallback([rawFrameQueue](std::unique_ptr<Frame> f) {
        rawFrameQueue->enqueue(std::move(f));
    });

    Frame::MemoryType frameType = (true == cfg.use_dmabuf)
    ? Frame::MemoryType::DMABUF
    : Frame::MemoryType::MMAP;

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
    RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;
    RgaProcessor::Config rgacfg{
        cctr, rawFrameQueue, frameQueue, cfg.width,
        cfg.height, frameType, RK_FORMAT_RGBA_8888, format, 10
    };
    RgaProcessor processor_(rgacfg) ;
    
    cctr->start();
    processor_.start();

    std::unique_ptr<Frame> frame;
    while (!frameQueue->try_dequeue(frame)){
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
    }
    std::cout << "frame Index:\t" << frame->meta.index << "\nframe fd:\t"
    << frame->dmabuf_fd() << "\nw:\t" << frame->meta.w << "\nh:\t" << frame->meta.h
    << "\t\n---\n";
    auto bufptr = frame->sharedState()->dmabuf_ptr;
    RgaProcessor::dumpDmabufAsRGBA(bufptr->fd(), frame->meta.w, frame->meta.h, bufptr->size(), bufptr->pitch(), "./end.rgba");

    processor_.releaseBuffer(frame->meta.index);
    processor_.stop();
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
        if (nullptr != buf) {
            std::cout << "[rawDmabuf] Prime fd: " << buf->fd() << ", Size: " << buf->size()
                << ", Width: " << buf->width() << ", Height: " << buf->height() << std::endl;
        } else {
            std::cerr << "Failed to create DmaBuffer\n";
            continue;
        }
        // 从 fd 导入
        auto ibuf = DmaBuffer::importFromFD(buf->fd(), buf->width(), buf->height(), buf->format());
        if (nullptr != ibuf) {
            std::cout << "[importDmabuf] Prime fd: " << ibuf->fd() << ", Size: " << ibuf->size()
                << ", Width: " << ibuf->width() << ", Height: " << ibuf->height() << std::endl;
        } else {
            std::cerr << "Failed to import DmaBuffer from fd\n";
        }
    }
    return 0;
}
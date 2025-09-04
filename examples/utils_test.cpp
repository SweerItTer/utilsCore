#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>

#include "rga/rgaProcessor.h"
#include "v4l2/cameraController.h"
#include "dma/dmaBuffer.h"
#include "drm/drmLayer.h"
#include "drm/planesCompositor.h"
#include "safeQueue.h"
#include "objectsPool.h"

using namespace DrmDev;

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
    int ret = 0;
    // 创建队列
    auto rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    auto frameQueue     	= std::make_shared<FrameQueue>(10);

    // 获取设备组合
    auto& devices = DrmDev::fd_ptr->getDevices();
    if (0 >= devices.size()){
        std::cout << "Get no devices." << std::endl;
        return -1;
    }
    // 取出第一个屏幕
    auto& dev = devices[0];
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
    << ", Resolution: " << dev->width << "x" << dev->height << "\n";

    // 相机配置
    CameraController::Config cfg = {
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = 1280,
        .height = 720,
        .format = V4L2_PIX_FMT_NV12
    };
    
    // 初始化相机控制器
    auto cctr         	= std::make_shared<CameraController>(cfg);
    if (!cctr) {
        std::cout << "Failed to create CameraController object.\n";
        return -1;
    }
    // 设置入队队列
    cctr->setFrameCallback([&rawFrameQueue](std::unique_ptr<Frame> f) {
        rawFrameQueue->enqueue(std::move(f));
    });

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
    RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;
    // 配置RGA参数
    RgaProcessor::Config rgacfg{
        cctr, rawFrameQueue, frameQueue, cfg.width,
        cfg.height, cfg.use_dmabuf, RK_FORMAT_RGBA_8888, format, 10
    };
    // 初始化转换线程
    RgaProcessor processor_(rgacfg) ;

    // 出队帧缓存
    std::unique_ptr<Frame> frame;
    cctr->start();
    processor_.start();

    while (1){
        if (!frameQueue->try_dequeue(frame)){
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        std::cout << "frame Index:\t" << frame->meta.index << "\nframe fd:\t"
            << frame->dmabuf_fd() << "\nw:\t" << frame->meta.w << "\nh:\t" << frame->meta.h
            << "\t\n---\n";
        processor_.releaseBuffer(frame->index());
    }
    // std::cout << "frame Index:\t" << frame->meta.index << "\nframe fd:\t"
    // << frame->dmabuf_fd() << "\nw:\t" << frame->meta.w << "\nh:\t" << frame->meta.h
    // << "\t\n---\n";
    // auto bufptr = frame->sharedState()->dmabuf_ptr;
    // RgaProcessor::dumpDmabufAsXXXX8888(bufptr->fd(), frame->meta.w, frame->meta.h, bufptr->size(), bufptr->pitch(), "./end.argb");

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
        DmaBufferPtr buf = DmaBuffer::create(1920, 1080, DRM_FORMAT_XRGB8888, 0);
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
        auto ibuf = DmaBuffer::importFromFD(buf->fd(), buf->width(), buf->height(), buf->format(), 0);
        if (nullptr != ibuf) {
            std::cout << "[importDmabuf] Prime fd: " << ibuf->fd() << ", Size: " << ibuf->size()
                << ", Width: " << ibuf->width() << ", Height: " << ibuf->height() << std::endl;
        } else {
            std::cerr << "Failed to import DmaBuffer from fd\n";
        }
    }
    return 0;
}

int layerTest(){
    auto& devices = DrmDev::fd_ptr->getDevices();
    if (devices.empty()) {
        std::cout << "Get no devices." << std::endl;
        return -1;
    }
    
    auto& dev = devices[0];
    if (0 == dev->connector_id) return -1;
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
    << ", Resolution: " << dev->width << "x" << dev->height << "\n";
    
    DmaBufferPtr dmabuf = DmaBuffer::create(dev->width, dev->height, DRM_FORMAT_XRGB8888, 0);

    DrmLayer layer({dmabuf}, 2);
    auto fbid = layer.getProperty("fbId").get<uint32_t>();
    std::cout << "FramebufferId: " << fbid << "\n";
    int ret = drmModeSetCrtc(DrmDev::fd_ptr->get(), 
                         dev->crtc_id, 
                         fbid,  
                         0, 0,
                         &dev->connector_id, 1, 
                         &dev->mode); // <-- 传指针

    std::cout << "ret : " << ret << "\n";
    // while(1); // 热插拔事件检测
    return ret;
}

int fbShowTest(){
    int ret = 0;
    // 创建队列
    auto rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    auto frameQueue     	= std::make_shared<FrameQueue>(10);

    // 获取设备组合
    auto& devices = DrmDev::fd_ptr->getDevices();
    if (0 >= devices.size()){
        std::cout << "Get no devices." << std::endl;
        return -1;
    }
    // 取出第一个屏幕
    auto& dev = devices[0];
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
    << ", Resolution: " << dev->width << "x" << dev->height << "\n";

    // 相机配置
    CameraController::Config cfg = {
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = 3840,
        .height = 2160,
        // .width = 1280,
        // .height = 720,
        .format = V4L2_PIX_FMT_NV12
    };
    
    // 初始化相机控制器
    auto cctr         	= std::make_shared<CameraController>(cfg);
    if (!cctr) {
        std::cout << "Failed to create CameraController object.\n";
        return -1;
    }
    // 设置入队队列
    cctr->setFrameCallback([rawFrameQueue](std::unique_ptr<Frame> f) {
        rawFrameQueue->enqueue(std::move(f));
    });

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
    RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;
    auto dstFormat = RK_FORMAT_RGBA_8888;
    // 配置RGA参数
    RgaProcessor::Config rgacfg{
        cctr, rawFrameQueue, frameQueue, cfg.width,
        cfg.height, cfg.use_dmabuf, dstFormat, format, 10
    };
    // 初始化转换线程
    RgaProcessor processor_(rgacfg) ;

    // 出队帧缓存
    std::unique_ptr<Frame> frame;

    // 导出合成器
    auto const& compositor = PlanesCompositor::create();
    if (!compositor){
        std::cout << "Failed to create PlanesCompositor object.\n";
        return -1;
    } 

    auto fx = [](uint32_t v){ return v << 16; };

    // 初始化layer
    std::vector<uint32_t> usingPlaneId;
    // 获取所有支持目标格式并且在指定CRTC上的Plane
    DrmDev::fd_ptr->refreshPlane(formatRGAtoDRM(dstFormat), dev->crtc_id);
    // 获取指定类型Plane
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, usingPlaneId);
    std::cout << "Gain " << usingPlaneId.size() <<" available planes";
    for(auto& id : usingPlaneId){
        std::cout << " " << id;
    }
    std::cout << ".\n";
    // return -1; // 测试用
    // 若无可以plane则退出
    if (usingPlaneId.empty()){ return -1; }
    // 初始化layer
    auto layer = std::make_shared<DrmLayer>(std::vector<DmaBufferPtr>(), 2);
    // 配置属性
    DrmLayer::LayerProperties props{
        .type_       = 0, 
        .plane_id_   = usingPlaneId[0],  
        .crtc_id_    = dev->crtc_id,
        .fb_id_      = 0, 

        // 源图像区域
        // src_* 使用左移 16
        .srcX_       = fx(0),
        .srcY_       = fx(0),
        .srcwidth_   = fx(cfg.width),
        .srcheight_  = fx(cfg.height),
        // 显示图像区域
        // crtc_* 不使用左移
        .crtcX_      = 0,
        .crtcY_      = 0,
        .crtcwidth_  = dev->width,
        .crtcheight_ = dev->height
    };
    // 设置属性
    layer->setProperty(props);
    // 设置更新回调
    layer->setUpdateCallback([&compositor](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId){
        compositor->updateLayer(layer, fbId);
    });
    // 将layer添加到合成器
    compositor->addLayer(layer);
    std::cout << "Layer initialized.\n"; 

    // 开启采集和图像转换
    cctr->start();
    processor_.start();
    int fence = -1;
    while (true) {
        // 取出一帧
        if (!frameQueue->try_dequeue(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        // 取出该帧的drmbuf
        auto dmabuf = frame->sharedState()->dmabuf_ptr;
        // 更新fb,同时回调触发合成器更新fbid
        layer->updateBuffer({dmabuf});
        // 提交一次
        compositor->commit(fence);
        wait_fence(fence, [&layer](){ layer->onFenceSignaled(); });
        
        // 释放drmbuf
        processor_.releaseBuffer(frame->meta.index);
    }
    
    processor_.releaseBuffer(frame->meta.index);
    processor_.stop();
    cctr->stop();

    return 0;
}

int drmDevicesControllerTest(){
    auto fd = DrmDev::fd_ptr;
    // 测试各项资源获取
    auto res = fd->getResources();
    auto planeRes = fd->getPlaneResources();
    if (!res || !planeRes) {
        std::cout << "Get resources faild\n";
        return -1;
    }
    std::cout << "From resources Get " << res->count_connectors << " connectors, "
    << res->count_encoders << " encoders, "
    << res->count_crtcs << " crtcs.\n";

    auto& devices = fd->getDevices();
    std::cout << "Get " << devices.size() << " devices combinations.\n";
    if (devices.empty()){
        std::cout << "Gets no device combined.\n"; 
        return -1;
    }
    
   size_t planeCount;
    for (const auto& dev : devices) {
        std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
        << ", Resolution: " << dev->width << "x" << dev->height << "\n";
        // 通过格式筛选
        planeCount = fd->refreshPlane(DRM_FORMAT_RGB888, dev->crtc_id);
        std::cout << "Find " << planeCount << " matched planes.\n\n";
    }
    // 通过类型筛选
    std::vector<uint32_t> getsPlanesIds;
    fd->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, getsPlanesIds);
    std::cout << "Find " << getsPlanesIds.size() << " matched OVERLAY planes.\n";
    if (getsPlanesIds.empty()) return -1;
    
    for (auto& id : getsPlanesIds){
        auto planeCache = fd->getPlaneById(id);
        if (!planeCache) {
            std::cout << "There is no plane for ID: " << id << "\n";
            continue;
        }
        std::cout << "Find Plane: " << id << ", Plane supported formats: \n";
        for (auto& format : planeCache->formats){
            std::cout << fourccToString(format) << ", \n";
        }
    }
    // 测试热插拔
    return 0;
}

int main(int argc, char const *argv[]) {
    DrmDev::fd_ptr = DeviceController::create();
    if (!DrmDev::fd_ptr) {
        std::cout << "Init DrmDev::fd_ptr faild\n";
        return -1;
    }
    int ret = 0;

    // 定义测试用例映射表
    // key: 命令行参数 (如 "--rgatest")
    // value: 一个无参数且返回 int 的函数对象（可以是函数指针、lambda等）
    std::unordered_map<std::string, std::function<int()>> testMap = {
        {"--rgatest", rgaTest},    // 直接使用函数指针
        {"--dmatest", dmabufTest},
        {"--layertest", layerTest},
        {"--devtest", drmDevicesControllerTest},
        {"--fbshow", []() { return fbShowTest(); }} // 也可以用 Lambda 包装
    };

    const std::string help_opt = "--help";

    // 处理帮助信息或无参数情况
    if (argc == 1 || std::string(argv[1]) == help_opt) {
        std::cout << "用法: " << argv[0] << " [选项]" << std::endl;
        std::cout << "选项:" << std::endl;
        for (const auto& pair : testMap) { // 遍历map，打印所有选项:cite[1]:cite[2]
            std::cout << "  " << pair.first << "   运行对应测试" << std::endl;
        }
        std::cout << "  " << help_opt << "     显示此帮助信息" << std::endl;
        return 0;
    }

    std::string inputArg = argv[1]; // 获取用户输入的参数

    // 在map中查找输入的参数
    auto it = testMap.find(inputArg);
    if (it != testMap.end()) {
        // 找到了对应的测试函数
        try {
            ret = it->second(); // 执行找到的函数
        } catch (const std::exception& e) {
            std::cerr << "运行时错误: " << e.what() << std::endl;
            ret = 1;
        } catch (...) {
            std::cerr << "未知错误发生" << std::endl;
            ret = 1;
        }
    } else {
        // 没有找到对应的命令
        std::cerr << "未知选项: " << inputArg << std::endl;
        std::cerr << "请使用 '" << help_opt << "' 查看可用选项。" << std::endl;
        return 1;
    }

    return ret;
}
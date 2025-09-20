#include <csignal>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>

#include <QGuiApplication>

#include "fbshow.h"
#include "rander/core.h"
#include "rander/draw.h"

using namespace DrmDev;

static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running = false;
    }
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
        planeCount = fd->refreshPlane(dev->crtc_id);
        std::cout << "Find " << planeCount << " matched planes.\n\n";
    }
    // 通过类型筛选
    std::vector<uint32_t> getsPlanesIds;
    fd->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, DRM_FORMAT_RGB888, getsPlanesIds);
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

auto infoPrinter = [](const std::vector<uint32_t>& Ids){
    std::cout << "Gain " << Ids.size() <<" usable planes";
    for(auto& id : Ids){
        std::cout << " " << id;
    }
    std::cout << ".\n";
};

uint32_t fx(uint32_t v){ return v << 16; }

bool fillDmaBuffer(DmaBufferPtr& buf)
{
    if(nullptr == buf) return false;

    int fd = buf->fd();
    size_t size = buf->size();  // DMABUF 的实际大小
    if(fd < 0 || size == 0) return false;

    // CPU 映射 DMABUF
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(data == MAP_FAILED) return false;

    // 填充测试内容, 比如绿色
    uint32_t* ptr = reinterpret_cast<uint32_t*>(data);
    uint32_t width = buf->width();
    uint32_t height = buf->height();
    uint32_t pitch = buf->pitch() / 4; // pitch 以像素计
    for(uint32_t y = 0; y < height; ++y){
        for(uint32_t x = 0; x < width; ++x){
            ptr[y * pitch + x] = 0xFF00FF00; // ARGB 绿色
        }
    }

    // 解除映射
    munmap(data, size);

    return true;
}

int GPUdrawTest(){
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t format = DRM_FORMAT_XRGB8888;
    // plane id 列表
    std::vector<uint32_t> usableOverlayPlaneIds;
    // 创建合成器
    auto compositor = std::move(PlanesCompositor::create());
    // 创建 layer
    auto layer = std::make_shared<DrmLayer>(std::vector<DmaBufferPtr>(), 2);
    // 获取可用设备组合
    auto devices = &(DrmDev::fd_ptr->getDevices());
    if (devices->empty()){ std::cout << "Get no devices.\n"; return -1; }
    // 取出第一个屏幕
    auto dev = (*devices)[0];
    if (nullptr == dev) { std::cout << "Failed to get devices.\n"; return -1; }
    // 获取所有在指定CRTC上的Plane
    DrmDev::fd_ptr->refreshPlane(dev->crtc_id);
    // 获取指定类型并且支持目标格式的 Plane
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, format, usableOverlayPlaneIds);
    // 输出相关信息
    infoPrinter(usableOverlayPlaneIds);
    // 若无可以plane则退出
    if (usableOverlayPlaneIds.empty())
    { std::cout << "Some plane do not matched.\n"; return -1; }

    // 配置属性
    DrmLayer::LayerProperties frameLayerProps{
        .plane_id_   = usableOverlayPlaneIds[0],  
        .crtc_id_    = dev->crtc_id,

        // 源图像区域
        // src_* 使用左移 16
        .srcX_       = fx(0),
        .srcY_       = fx(0),
        .srcwidth_   = fx(width),
        .srcheight_  = fx(height),
        // 显示图像区域
        // crtc_* 不使用左移
        .crtcX_      = 0,
        .crtcY_      = 0,
        // 自动缩放
        .crtcwidth_  = dev->width,
        .crtcheight_ = dev->height
    };
    // 初始化layer属性
    layer->setProperty(frameLayerProps);
    // 注册更新回调
    layer->setUpdateCallback([&compositor](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId){
        compositor->updateLayer(layer, fbId);
    });
    // 将layer添加到合成器
    compositor->addLayer(layer);
    std::cout << "Layer initialized.\n";
    // 申请管理核心
    auto& core = Core::instance();

    // 注册 Core
    core.registerResSlot("test", 2, std::move(DmaBuffer::create(width, height, format, 0)));
    for (int i = 0; i < 300; ++i) {  // 300 帧,大概 5 秒
        int OpenGLFence = 0;
        int DRMFence = 0;
        // 取出一个可用buffer
        auto slot = core.acquireFreeSlot("test");
        if (nullptr == slot) { continue; }

        // 清空并绘制不同的内容
        QString text = QString("Frame %1").arg(i);
        Draw::clear(*(slot.get()));
        Draw::drawText(*(slot.get()), text, QPointF(slot->width()/2, slot->height()/2));

        // 同步内容到 dmabuf
        if (!slot->syncToDmaBuf(OpenGLFence)) {
            std::cout << "Failed to sync dmabuf. \n";
            core.releaseSlot("test", slot);
            continue;
        }

        // 等待绘制和显示
        FenceWatcher::instance().watchFence(OpenGLFence, [slot, layer, &compositor, &DRMFence]() {
            layer->updateBuffer({slot->dmabufPtr});
            compositor->commit(DRMFence);
            FenceWatcher::instance().watchFence(DRMFence, [layer]() {
                layer->onFenceSignaled();
            });
        });

        // 控制帧率,比如 60fps
        usleep(16666);
        core.releaseSlot("test", slot);
        if (!running) break;
    }
    return 0;
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);


    DrmDev::fd_ptr = DeviceController::create();
    if (!DrmDev::fd_ptr) {
        std::cout << "Init DrmDev::fd_ptr faild\n";
        return -1;
    }
    int ret = 0;
    std::signal(SIGINT, handleSignal);

    // 定义测试用例映射表
    // key: 命令行参数 (如 "--rgatest")
    // value: 一个无参数且返回 int 的函数对象（可以是函数指针、lambda等）
    std::unordered_map<std::string, std::function<int()>> testMap = {
        {"--rgatest", rgaTest},    // 直接使用函数指针
        {"--dmatest", dmabufTest},
        {"--layertest", layerTest},
        {"--devtest", drmDevicesControllerTest},
        {"--FBOtest", GPUdrawTest},
        {"--fbshow", [](){ 
            FrameBufferTest test;
            test.start();
            GPUdrawTest();
            while(running){ sleep(1000); }
            test.stop();
            return 0;
        }} // 也可以用 Lambda 包装
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
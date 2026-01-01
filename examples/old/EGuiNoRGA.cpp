/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:12:18
 * @FilePath: /EdgeVision/examples/old/EGuiNoRGA.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "EGui.h"

// 捕获处理初始化
void FrameBufferTest::initVisionPipeline() {
    cameraInit();
    rgaInit();
}

void FrameBufferTest::cameraInit() {
    if (nullptr != cameraCapturer) {
        cameraCapturer.reset();
    }
    // 获取实际屏幕输出大小
    auto captureRes = chooseClosestResolution(dev->width, dev->height);
    autoWidth = captureRes.first;
    autoHeight = captureRes.second;
    // 捕获配置
    cameraConfig = {
        .buffer_count = 4,
        .plane_count = 1,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = autoWidth,
        .height = autoHeight,
        .format = captureFormat
    };
    // 初始化视频捕获类
    cameraCapturer.reset( new CameraController(cameraConfig) );
    if (!cameraCapturer) { std::cout << "Failed to create CameraController object.\n"; return; }
    // 设置回调入队队列
    cameraCapturer->setFrameCallback([this](FramePtr f) {
        rawFrameQueue->enqueue(f);
    });
}
void FrameBufferTest::rgaInit(){}
// 构造函数
FrameBufferTest::FrameBufferTest(){
    // 创建原始NV12帧队列和RGBA帧队列
    rawFrameQueue  	= std::make_shared<FrameQueue>(20); 
    // 导出合成器
    compositor = std::move(PlanesCompositor::create());
    if (!compositor){ std::cout << "Failed to create PlanesCompositor object.\n"; return; }
    // 热插拔回调, 先释放资源再重新获取
    DrmDev::fd_ptr->registerResourceCallback(
        std::bind(&FrameBufferTest::preRefresh, this),
        std::bind(&FrameBufferTest::postRefresh, this)
    );
    postRefresh(); // 初始刷新
}

// 释放资源(devices/planes)
void FrameBufferTest::preRefresh(){
    if (running) refreshing = true;
    else refreshing = false;
    mouseWatcher.stop();
    // 关闭服务
    cameraCapturer.reset(); 
    // 清空当前队列
    FramePtr tempFrame;
    while (rawFrameQueue->try_dequeue(tempFrame)) {}
    // 移除所有图层
    compositor->removeAllLayer();
    devices->clear(); // 清空设备组合
    dev.reset(); // 清空当前设备组合
}

// 重新获取资源(devices/planes)
void FrameBufferTest::postRefresh(){
    // 获取设备组合
    devices = &(DrmDev::fd_ptr->getDevices());
    if (devices->empty()){
        std::cout << "[DrmDev] Get no devices." << std::endl;
        refreshing = true;	// 若无外接屏幕需等待直至有屏幕可用
        return;
    }
    // 取出第一个可用设备组合
    dev = (*devices)[0];
    if (!dev) {
        std::cout << "Failed to get usable device." << std::endl;
        refreshing = true;	// 若无外接屏幕需等待直至有屏幕可用
        return;
    }
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
        << ", Resolution: " << dev->width << "x" << dev->height << "\n";
    
    // 重新配置主要后台服务
    initVisionPipeline();	
    // 获取所有在指定CRTC上的Plane
    DrmDev::fd_ptr->refreshPlane(dev->crtc_id);
    // 初始化 id 列表
    std::vector<uint32_t> usablePrimaryPlaneIds;
    std::vector<uint32_t> usableOverlayPlaneIds;
    // 获取指定类型并且支持目标格式的 Plane
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_PRIMARY, convertRGAtoDrmFormat(primaryFormat), usablePrimaryPlaneIds);
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, convertRGAtoDrmFormat(overlayFormat), usableOverlayPlaneIds);
    infoPrinter(usablePrimaryPlaneIds);
    infoPrinter(usableOverlayPlaneIds);
    // return -1; // 查询所有格式时用

    if (usablePrimaryPlaneIds.empty() || usableOverlayPlaneIds.empty())// 若无可以plane则退出
    { std::cout << "Some plane do not matched.\n"; return; }

    // 初始化 layer
    primaryLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );	// 双缓冲
    overLayer.reset( new DrmLayer(std::vector<DmaBufferPtr>(), 2) );
    // 配置属性
    DrmLayer::LayerProperties OverLayerProps{
        // NV12
        .plane_id_   = usableOverlayPlaneIds[0],  
        .crtc_id_    = dev->crtc_id,

        // 源图像区域
        // src_* 使用左移 16
        .srcX_       = fx(0),
        .srcY_       = fx(0),
        .srcwidth_   = fx(autoWidth),
        .srcheight_  = fx(autoHeight),
        // 显示图像区域
        // crtc_* 不使用左移
        .crtcX_      = 0,
        .crtcY_      = 0,
        // 自动缩放
        .crtcwidth_  = dev->width,
        .crtcheight_ = dev->height,
        .zOrder_	 = 0
    };
    // 配置属性
    DrmLayer::LayerProperties PrimaryLayerProps = OverLayerProps;
    PrimaryLayerProps.plane_id_ = usablePrimaryPlaneIds[0];
    PrimaryLayerProps.zOrder_   = 1;
    initLayer(primaryLayer, PrimaryLayerProps);
    initLayer(overLayer, OverLayerProps);
    
    // 将layer添加到合成器
    compositor->addLayer(primaryLayer);
    compositor->addLayer(overLayer);
    std::cout << "Layer initialized.\n"; 

    // 服务重启
    cameraCapturer->start();
    cameraCapturer->setThreadAffinity(1); // 绑定到核心1
    mouseWatcher.setScreenSize(dev->width, dev->height);
    mouseWatcher.start();
    // 刷新完成
    refreshing = false;
}

FrameBufferTest::~FrameBufferTest(){
    stop();
    devices = nullptr;
}

void FrameBufferTest::start(){
    if (running) return;
    running.store(true);
    thread_ = std::thread(&FrameBufferTest::run, this);
}

void FrameBufferTest::stop(){
    if (running.load() == false) return;
    running.store(false);
    if (thread_.joinable()){
        thread_.join();
    }
    fprintf(stdout, "Thread joined.\n");
    preRefresh();
}

void FrameBufferTest::RunUI(int &argc, char *argv[]){
    // qt 相关初始化
    QApplication app(argc, argv);
    // 环境配置
    qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", QByteArray("1"));
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", QByteArray("minimal"));
    // 使用静态变量接收手动退出 (lambda 转 void* 不能捕获对象)
    static QApplication* staticApp = &app;
    std::signal(SIGINT, [](int signal) {
        if (signal == SIGINT && staticApp) {
            std::cout << "Ctrl+C received, stopping..." << std::endl;
            staticApp->quit();
        }
    });
    ThreadUtils::bindCurrentThreadToCore(3);// 绑定UI绘制线程到CPU CORE 3

    // UI界面实例化 
    mainInterface = std::make_shared<MainInterface>();
    // 注册UI指针到唤醒线程
    mouseWatcher.setNotifyWindow(mainInterface.get());

    // 样式/位置
    double dpiScale = 1.0;
    QColor cursorColor(255, 0, 0);
    QRect uiRact;           // UI 显示位置
    DrawRect uiDrawRect;    // UI 实际位置及缩放比例

    // 鼠标图标/默认位置
    QString cursor = ",";
    int baseSize = 32;
    int autoCursorSize = baseSize;
    int x = 10;
    int y = 10;

    auto& core = Core::instance();				// 获取渲染 core
    auto& draw = Draw::instance();				// 获取绘制 draw
    bool needUpdate = false;				    // slot 更新标志
    std::string slotType = "UI&Cursor";			// slot命名

    auto updateSlot = [&](){
        auto dmabufTemplate = DmaBuffer::create(	// 创建 dmabuf 模板
            autoWidth, autoHeight, convertRGAtoDrmFormat(primaryFormat), 0, 0);
        if (!dmabufTemplate){
            std::cout << "Failed to create dmabuf template.\n";
            return;
        }
        core.registerResSlot(slotType, 2, std::move(dmabufTemplate));   // 注册 slot
        dpiScale = MainInterface::computeDPIScale(autoWidth, autoHeight);    // DPI缩放
        
        int windowWidth  = static_cast<int>(mainInterface->width() * dpiScale);
        int windowHeight = static_cast<int>(mainInterface->height() * dpiScale);

        uiRact = QRect(0, autoHeight - windowHeight, windowWidth, windowHeight);
        autoCursorSize = baseSize * dpiScale;
        needUpdate = false;
    };
    // 初始化 slot
    updateSlot();
    
    QTimer renderTimer, systemMoniterTimer;
    QObject::connect(&renderTimer, &QTimer::timeout, [&] {
        // 等待刷新完成
        if (true == refreshing){
            needUpdate = true;
            return;
        }
        if (needUpdate) updateSlot();
        // Fence 状态
        int DrawFence = -1;
        auto slot = core.acquireFreeSlot(slotType, 33);	// 取出一个 slot
        if (nullptr == slot || !slot->qfbo.get()) {
            std::cout << "Failed to acquire slot.\n";
            return;
        }
         // 绘制光标
        mouseWatcher.getPosition(x, y);	// 获取鼠标坐标
        draw.clear(slot->qfbo.get());
        // 绘制UI界面
        uiDrawRect = draw.drawWidget(*(slot.get()), mainInterface.get(), uiRact); 
        if (!uiDrawRect.rect.isEmpty()) mainInterface->setUiDrawRect(uiDrawRect.rect, uiDrawRect.scale);
       
        draw.drawText(*(slot.get()), cursor, QPointF(x, y), cursorColor, autoCursorSize);	
        // 同步内容到 dmabuf
        if (!slot->syncToDmaBuf(DrawFence)) {
            std::cout << "Failed to sync dmabuf. \n";
            core.releaseSlot(slotType, slot);
            return;
        };
        if (slot->dmabufPtr == nullptr) {
            std::cout << "Slot dmabuf is null.\n";
            core.releaseSlot(slotType, slot);
            return;
        }
        // 等待绘制完成
        FenceWatcher::instance().watchFence(DrawFence, [&]() {
            primaryLayer->updateBuffer({slot->dmabufPtr});
            // 释放 slot
            core.releaseSlot(slotType, slot);
        });
    });
    // 录像
    QObject::connect(mainInterface.get(), &MainInterface::recordSignal, [](bool status){
        qDebug() << QString("Record statu:%1\n").arg(status);
    });
    // 更新数据
    QObject::connect(&systemMoniterTimer, &QTimer::timeout, [&] {
        mainInterface->updateCPUpayload(CPUMonitor.getUsage());
        mainInterface->updateMemoryUsage(MMonitor.getUsage());
        mainInterface->updateFPS(fps.load());
    });
    // 开启计时器
    renderTimer.start(33);
    systemMoniterTimer.start(1000);
    this->start();
    // 启动主事件循环
    app.exec();
    // 关闭计时器
    systemMoniterTimer.stop();
    renderTimer.stop();
    // 关闭渲染资源
    Draw::instance().shutdown();
    Core::instance().shutdown();
}

void FrameBufferTest::RunUI(int &argc, char *argv[]){
    // qt 相关初始化
    QApplication app(argc, argv);
    
    // 使用静态变量接收手动退出 (lambda 转 void* 不能捕获对象)
    static QApplication* staticApp = &app;
    std::signal(SIGINT, [](int signal) {
        if (signal == SIGINT && staticApp) {
            std::cout << "Ctrl+C received, stopping..." << std::endl;
            staticApp->quit();
        }
    });
    
    // UI界面实例化 
    mainInterface = std::make_shared<MainInterface>();
    
    QRect uiRact;           // UI 显示位置
    DrawRect uiDrawRect;    // UI 实际位置及缩放比例

    auto& core = Core::instance();				// 获取渲染 core
    auto& draw = Draw::instance();				// 获取绘制 draw
    bool needUpdate = false;				    // slot 更新标志
    std::string slotType = "UI&Cursor";			// slot命名

    auto updateSlot = [&](){
        auto dmabufTemplate = DmaBuffer::create(	// 创建 dmabuf 模板
            autoWidth, autoHeight, convertRGAtoDrmFormat(primaryFormat), 0, 0);
        if (!dmabufTemplate){
            std::cout << "Failed to create dmabuf template.\n";
            return;
        }
        core.registerResSlot(slotType, 2, std::move(dmabufTemplate));   // 注册 slot
        needUpdate = false;
    };
    // 初始化 slot
    updateSlot();
    
    QTimer renderTimer;
    QObject::connect(&renderTimer, &QTimer::timeout, [&] {
        // 等待刷新完成
        if (true == refreshing){
            needUpdate = true;
            return;
        }
        if (needUpdate) updateSlot();
        // Fence 状态
        int DrawFence = -1;
        auto slot = core.acquireFreeSlot(slotType, 33);	// 取出一个 slot
        if (nullptr == slot || !slot->qfbo.get()) {
            std::cout << "Failed to acquire slot.\n";
            return;
        }
        draw.clear(slot->qfbo.get());
        // 绘制UI界面
        uiDrawRect = draw.drawWidget(*(slot.get()), mainInterface.get(), uiRact); 
        if (!uiDrawRect.rect.isEmpty()) mainInterface->setUiDrawRect(uiDrawRect.rect, uiDrawRect.scale);
        // 同步内容到 dmabuf
        if (!slot->syncToDmaBuf(DrawFence)) {
            std::cout << "Failed to sync dmabuf. \n";
            core.releaseSlot(slotType, slot);
            return;
        };
        if (slot->dmabufPtr == nullptr) {
            std::cout << "Slot dmabuf is null.\n";
            core.releaseSlot(slotType, slot);
            return;
        }
        // 等待绘制完成
        FenceWatcher::instance().watchFence(DrawFence, [&]() {
            primaryLayer->updateBuffer({slot->dmabufPtr});
            // 释放 slot
            core.releaseSlot(slotType, slot);
        });
    });
    // 开启计时器
    renderTimer.start(33);
    // 启动主事件循环
    app.exec();
    // 关闭计时器
    renderTimer.stop();
    // 关闭渲染资源
    Draw::instance().shutdown();
    Core::instance().shutdown();
}


void FrameBufferTest::run() {
    ThreadUtils::bindCurrentThreadToCore(0);  // 绑定显示线程到CPU CORE 0
    std::cout << "DRM show thread TID: " << syscall(SYS_gettid) << "\n";

    int frames = 0;
    std::vector<DmaBufferPtr> buffers;
    buffers.reserve(2); // 预分配内存

    auto beforeTime = std::chrono::steady_clock::now();
    while (running) {
        if (refreshing) {
            std::this_thread::yield();
            continue;
        }
        
        // --- dump NV12 图像 ---
        buffers.clear();
        FramePtr frame_NV12; // 自动释放drmbuf
        if (!rawFrameQueue->try_dequeue(frame_NV12)) continue;

        // 取出该帧的drmbuf
        auto Y_plane = frame_NV12->sharedState(0)->dmabuf_ptr;
        auto UV_plane = DmaBuffer::importFromFD(
            Y_plane->fd(),
            Y_plane->width(),
            Y_plane->height() / 2,
            Y_plane->format(),
            Y_plane->pitch() * Y_plane->height() / 2,
            Y_plane->pitch() * Y_plane->height()
        );
        buffers.emplace_back(std::move(Y_plane));
        buffers.emplace_back(std::move(UV_plane));
        // 更新layer
        overLayer->updateBuffer(buffers);

        // --- DRM 提交/同步 ---
        int DRMFence = -1;
        compositor->commit(DRMFence);
        FenceWatcher::instance().watchFence(DRMFence, [&]() {
            primaryLayer->onFenceSignaled();
            overLayer->onFenceSignaled();
            
            // --- 计算平均帧率 ---
            if (++frames % 30 == 0) {
                auto now = std::chrono::steady_clock::now();
                double intervalMs = std::chrono::duration<double, std::milli>(now - beforeTime).count();
                fps = 30.0 / (intervalMs / 1000.0);
                beforeTime = now;
            }
        });
    }
}

// planes 信息输出
void FrameBufferTest::infoPrinter(const std::vector<uint32_t>& Ids){
    std::cout << "Gain " << Ids.size() <<" usable planes";
    for(auto& id : Ids){
        std::cout << " " << id;
    }
    std::cout << ".\n";
};

// 初始化 layer
void FrameBufferTest::initLayer(std::shared_ptr<DrmLayer>& layer, DrmLayer::LayerProperties& layerProps){
    // 设置属性
    layer->setProperty(layerProps);
    // 设置更新回调
    layer->setUpdateCallback([this](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId){
        // 更新 fb
        compositor->updateLayer(layer, fbId);
    });
};

#include "EGui.h"

// 捕获处理初始化
void FrameBufferTest::initVisionPipeline() {
    cameraInit();
    rgaInit();
}

void FrameBufferTest::cameraInit() {
    // 获取实际屏幕输出大小
    auto captureRes = chooseClosestResolution(dev->width, dev->height);
    autoWidth = captureRes.first;
    autoHeight = captureRes.second;
    // 捕获配置
    cameraConfig = {
        .buffer_count = 2,
        .plane_count = 2,
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
        forRGAFrameQueue->enqueue(f);
        rawFrameQueue->enqueue(f);
    });
}

void FrameBufferTest::rgaInit(){
    // 初始化 RGA 处理线程
    rgaCfg.cctr = cameraCapturer;
    rgaCfg.rawQueue = forRGAFrameQueue;
    rgaCfg.width = cameraConfig.width;
    rgaCfg.height = cameraConfig.height;
    rgaCfg.usingDMABUF = cameraConfig.use_dmabuf;
    rgaCfg.srcFormat = overlayFormat;
    rgaCfg.dstFormat = primaryFormat;
    rgaCfg.poolSize = 4;
    processor.reset( new RgaProcessor(rgaCfg) );
}

// 构造函数
FrameBufferTest::FrameBufferTest(){
    // 创建原始NV12帧队列和RGBA帧队列
    rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    forRGAFrameQueue = std::make_shared<FrameQueue>(2);
    rknnPool_ = std::make_shared<rknnPool<Yolov5s, DmaBufferPtr, object_detect_result_list>>(
        "./yolov5s_relu.rknn", "./coco_80_labels_list.txt", 5	// 模型池大小
    );
    // 导出合成器
    compositor = std::move(PlanesCompositor::create());
    if (!compositor){ std::cout << "Failed to create PlanesCompositor object.\n"; return; }
    if (rknnPool_->init() != 0){
        std::cout << "Failed to initialize rknnPool.\n";
        return;
    }
    // 热插拔回调, 先释放资源再重新获取
    DrmDev::fd_ptr->registerResourceCallback(
        std::bind(&FrameBufferTest::preRefresh, this),
        std::bind(&FrameBufferTest::postRefresh, this)
    );
    postRefresh(); // 初始刷新
}

// 释放资源(devices/planes)
void FrameBufferTest::preRefresh(){
    if (running) refreshing = true; else refreshing = false;
    mouseWatcher.stop();
    // 析构线程
    processor.reset();
    cameraCapturer.reset(); 
    // 清空当前队列
    FramePtr tempFrame;
    while (rawFrameQueue->size_approx() > 0) {
        rawFrameQueue->try_dequeue(tempFrame);
    }
    while (forRGAFrameQueue->size_approx() > 0) {
        forRGAFrameQueue->try_dequeue(tempFrame);
    }
    // 移除所有图层
    compositor->removeAllLayer();
    rknnPool_->clearFutures(); // 清空所有 future
    devices->clear(); // 清空设备组合
    dev.reset(); // 清空当前设备组合
}

// 重新获取资源(devices/planes)
void FrameBufferTest::postRefresh(){
    // 获取设备组合
    devices = &(DrmDev::fd_ptr->getDevices());
    if (devices->empty()){
        std::cout << "Get no devices." << std::endl;
        refreshing = true;	// 若无外接屏幕需等待直至有屏幕可用
        return;
    }
    // 取出第一个可用设备组合
    dev = (*devices)[0];
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
        << ", Resolution: " << dev->width << "x" << dev->height << "\n";

    initVisionPipeline();	// 重新配置主要线程
    // 获取所有在指定CRTC上的Plane
    DrmDev::fd_ptr->refreshPlane(dev->crtc_id);
    // 初始化 id 列表
    std::vector<uint32_t> usablePrimaryPlaneIds;
    std::vector<uint32_t> usableOverlayPlaneIds;
    // 获取指定类型并且支持目标格式的 Plane
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_PRIMARY, formatRGAtoDRM(primaryFormat), usablePrimaryPlaneIds);
    DrmDev::fd_ptr->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, formatRGAtoDRM(overlayFormat), usableOverlayPlaneIds);
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
    
    // 重新获取资源后重启
    cameraCapturer->start();
    cameraCapturer->setThreadAffinity(1); // 绑定到核心1
    processor->start();
    processor->setThreadAffinity(1); // 绑定到核心1
    mouseWatcher.setScreenSize(dev->width, dev->height);
    mouseWatcher.start();
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

void FrameBufferTest::RunUI(int &argc, char **argv){
    // qt 相关初始化
    QApplication app(argc, argv);
    
    // 使用静态变量 (lambda 转 void* 不能捕获对象)
    static QApplication* staticApp = &app;
    std::signal(SIGINT, [](int signal) {
        if (signal == SIGINT && staticApp) {
            std::cout << "Ctrl+C received, stopping..." << std::endl;
            staticApp->quit();
        }
    });
        
    qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", QByteArray("1"));
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", QByteArray("minimal"));
    qputenv("QT_SCALE_FACTOR", QByteArray::number(2));

    ThreadUtils::bindCurrentThreadToCore(3);// 绑定UI绘制线程到CPU CORE 3
    bool needUpdate = false;				// slot 更新标志
    object_detect_result_list yoloOutput_; 	// 模型输出结果引用
    std::vector<DrawBox> drawBoxs;			// 绘制框列表

    // 注册UI指针到唤醒线程
    mainInterface = std::make_shared<MainInterface>();
    auto winSize = mainInterface->size();
    mouseWatcher.setNotifyWindow(mainInterface.get());

    // 样式/位置
    QPointF fpsShowPos(10, 45);
    QColor fpsColor(255, 0, 0);
    QColor cursorColor(255, 0, 0);
    QRect uiRact(0, autoHeight - winSize.height(), winSize.width(), winSize.height());
    DrawRect uiDrawRect;
    // 鼠标图标/默认位置
    QString cursor = ".";
    int x = 10;
    int y = 10;

    auto& core = Core::instance();				// 获取渲染 core
    auto& draw = Draw::instance();				// 获取绘制 draw
    std::string slotType = "UI&Yolo";			// slot命名

    auto updateSlot = [this, &slotType](){
        auto dmabufTemplate = DmaBuffer::create(	// 创建 dmabuf 模板
            autoWidth, autoHeight, formatRGAtoDRM(primaryFormat), 0);
        if (!dmabufTemplate){
            std::cout << "Failed to create dmabuf template.\n";
            return;
        }
        Core::instance().registerResSlot(slotType, 2, std::move(dmabufTemplate)); // 注册 slot
    };
    updateSlot();							// 初始化 slot		
    
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&](){
        // 等待刷新完成
        if (true == refreshing){
            needUpdate = true;
            return;
        }
        if (needUpdate){ 							// 重新获取 slot
            updateSlot();
            needUpdate = false;
        }
        int OpenGLFence = -1;
        auto slot = core.acquireFreeSlot(slotType);	// 取出一个 slot
        if (nullptr == slot) {
            std::cout << "Failed to acquire slot.\n";
            return;
        }
        drawBoxs.clear();
        rknnPool_->get(yoloOutput_, 0);							// 获取最新的模型结果
        mouseWatcher.getPosition(x, y);							// 获取鼠标坐标
        QString qfps = QString("Fps: %1/s").arg(fps.load());	// 准备帧率文本

        if (yoloOutput_.empty() == false){
            for (auto& result : yoloOutput_){											
                QRectF boxRect( result.box.x, result.box.y, result.box.w, result.box.h);
                QString label = QString("%1: %2%")
                    .arg(QString::fromStdString(result.class_name))
                    .arg(int(result.prop * 100));
                QColor boxColor = QColor::fromRgb(
                    rand() % 256, rand() % 256, rand() % 256);
                DrawBox drawBox(boxRect, boxColor, label);
                drawBoxs.emplace_back(std::move(drawBox));
            }
        }
        draw.clear(slot->qfbo.get());
        draw.drawBoxes(*(slot.get()), drawBoxs);								// 绘制检测框
        draw.drawText(*(slot.get()), qfps, fpsShowPos, fpsColor);			// 绘制帧率
        uiDrawRect = draw.drawWidget(*(slot.get()), mainInterface.get(), uiRact); // 绘制UI界面
        if (!uiDrawRect.rect.isEmpty()) mainInterface->setUiDrawRect(uiDrawRect.rect, uiDrawRect.scale);
        draw.drawText(*(slot.get()), cursor, QPointF(x, y), cursorColor, 32);	// 绘制光标
        // 同步内容到 dmabuf
        if (!slot->syncToDmaBuf(OpenGLFence)) {
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
        FenceWatcher::instance().watchFence(OpenGLFence, [this, slot]() {
            primaryLayer->updateBuffer({slot->dmabufPtr});
        });
        // 释放 slot
        core.releaseSlot(slotType, slot);
    });

    timer.start(33);
    
    // 启动主事件循环
    app.exec();

    // 退出时清理
    timer.stop();
    // 关闭渲染资源
    Draw::instance().shutdown();
    Core::instance().shutdown();
}

void FrameBufferTest::run() {
    ThreadUtils::bindCurrentThreadToCore(0);  // 绑定显示线程到CPU CORE 0
    std::cout << "DRM show thread TID: " << syscall(SYS_gettid) << "\n";

    int frames = 0;
    auto beforeTime = std::chrono::steady_clock::now();
    while (running) {
        if (refreshing) {
            std::this_thread::yield();
            continue;
        }
        
        // --- dump NV12 图像 ---
        FramePtr frame_NV12; // 自动释放drmbuf
        std::vector<DmaBufferPtr> buffers;
        buffers.reserve(2); // 预分配内存
        if (!rawFrameQueue->try_dequeue(frame_NV12)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        // 取出该帧的drmbuf
        auto dmabuf = frame_NV12->sharedState(0)->dmabuf_ptr;
        auto dmabuf2 = DmaBuffer::importFromFD(
            dmabuf->fd(),
            dmabuf->width(),
            dmabuf->height(),
            dmabuf->format(),
            dmabuf->size(),
            dmabuf->width() * dmabuf->height()
        );
        buffers.push_back(dmabuf);
        buffers.push_back(dmabuf2);
        // 更新layer
        overLayer->updateBuffer(std::move(buffers));

        // --- 入队推理线程池 ---
        FramePtr frame_RGBA;
        if (processor->dump(frame_RGBA) == 0){
            rknnPool_->put(frame_RGBA->sharedState()->dmabuf_ptr);  // 异步提交，不等待结果
        }

        // --- DRM 提交/同步 ---
        int DRMFence = -1;
        compositor->commit(DRMFence);
        FenceWatcher::instance().watchFence(DRMFence, [&]() {
            primaryLayer->onFenceSignaled();
            overLayer->onFenceSignaled();
            
            // --- 计算平均帧率 ---
            frames++;
            if (frames % 10 == 0) {
                auto now = std::chrono::steady_clock::now();
                double intervalMs = std::chrono::duration<double, std::milli>(now - beforeTime).count();
                fps = 10.0 / (intervalMs / 1000.0);
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

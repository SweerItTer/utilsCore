/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:58:03
 * @FilePath: /EdgeVision/src/pipeline/appController.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:58:03
 * @FilePath: /EdgeVision/src/pipeline/appController.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "appController.h"
#include "threadUtils.h"
#include "threadPauser.h"
#include "fenceWatcher.h"

#include "displayManager.h"
#include "visionPipeline.h"
#include "yoloProcessor.h"

#include "uiRenderer.h"

#include "ConfigInterface/maininterface.h"

// 选择最接近屏幕分辨率的标准分辨率
static auto chooseClosestResolution(int screenW, int screenH) -> std::pair<int, int> {
    static const std::vector<std::pair<int, int>> standardRes = {
        {640, 480}, {720, 480}, {720, 576}, {1280, 720},
        {1920, 1080}, {2560, 1440}, {3840, 2160}, {4096, 2160}
    };

    std::pair<int, int> bestRes;
    int minDist = std::numeric_limits<int>::max();

    for (const auto& res : standardRes) {
        int dw = res.first - screenW;
        int dh = res.second - screenH;
        int dist = dw * dw + dh * dh;

        if (dist < minDist) {
            minDist = dist;
            bestRes = res;
        }
    }

    // 对齐 NV12
    int wAligned = (bestRes.first + 3) & ~3;
    int hAligned = (bestRes.second + 1) & ~1;

    return std::pair<int, int>(wAligned, hAligned);
}

class AppContriller::Impl {
private:
    // 视频捕获/显示管线/可视化界面/推理
    std::unique_ptr<VisionPipeline> vision_;
    std::shared_ptr<DisplayManager> display_;
    std::unique_ptr<UIRenderer> uiRenderer;
    std::unique_ptr<YoloProcessor> yoloProcessor;
    
	std::atomic<float> fps{0.0};                // 视频捕获线程FPS标志
    std::atomic_bool running{true};             // 视频线程运行标志
    // std::atomic_bool refreshing{false};	        // 刷新标志
    ThreadPauser refreshing;
    std::thread mainLoop;

    DisplayManager::PlaneHandle primaryPlaneHandle, overlayPlaneHandle;
    std::atomic<uint32_t> autoWidth, autoHeight;
public:
    Impl();
    ~Impl();

    void start();
    void quit();
private:
    // 视频线程
    void loop();
    // 热插拔回调 
    void preProcess();
    void postProcess();
    // qt信号绑定
    void signalBind();
};

// ------------------ 构造 / 析构 ------------------
AppContriller::Impl::Impl() {
    // DisplayManager 管理 HDMI / DRM planes
    display_  = std::make_shared<DisplayManager>();
    // UIrenderer 管理qt渲染和绘制
    uiRenderer = std::make_unique<UIRenderer>();

    if (!display_ || !uiRenderer) {
        std::cerr << "[AppContriller][ERROR] Failed to create DisplayManager or UIrenderer" << std::endl;
        throw std::runtime_error("AppContriller error when init.\n");
    }
    // 绑定输出
    uiRenderer->bindDisplayer(display_);
    // 加载光标
    uiRenderer->loadCursorIcon("./cursor-64.png");

    // YOLO 推理池
    yoloProcessor = std::make_unique<YoloProcessor>("./yolov5s_relu.rknn", "./coco_80_labels_list.txt", 5);
    // 注册回调
    display_->registerPostRefreshCallback(std::bind(&AppContriller::Impl::postProcess, this));
    display_->registerPreRefreshCallback(std::bind(&AppContriller::Impl::preProcess, this));
}

AppContriller::Impl::~Impl() {
    quit();
    // 若 throw 异常, 不会析构, 但是静态局部变量在函数作用域内, 只有函数被调用时才初始化
    // 清理渲染资源(只在程序退出时析构)
    if (uiRenderer) {
        Draw::instance().shutdown();
        Core::instance().shutdown();
    }
};

// ------------------ 资源回收 / 重置资源 ------------------
void AppContriller::Impl::preProcess(){
    refreshing.pause();
    if (yoloProcessor) yoloProcessor->pause();

    if(vision_) vision_->pause();
    // 暂停渲染
    if (uiRenderer) uiRenderer->pause(true);
}

void AppContriller::Impl::postProcess(){
    // 未初始化
    if (!display_) {
        std::cerr << "[AppContriller][ERROR] DisplayManager not init!" << std::endl;
        return;
    }

    // 获取合适的分辨率
    auto screenSize = display_->getCurrentScreenSize();
    auto _ = chooseClosestResolution(screenSize.first, screenSize.second);
    autoWidth = _.first; autoHeight = _.second;

    // 重新获取PlaneHandle
    DisplayManager::PlaneConfig overlayCfg = {
        .type = DisplayManager::PlaneType::OVERLAY,
        .srcWidth = autoWidth,
        .srcHeight = autoHeight,
        .zOrder = 0
    };
    DisplayManager::PlaneConfig primaryCfg = {
        .type = DisplayManager::PlaneType::PRIMARY,
        .srcWidth = autoWidth,
        .srcHeight = autoHeight,
        .zOrder = 1
    };
    overlayPlaneHandle = display_->createPlane(overlayCfg);
    primaryPlaneHandle = display_->createPlane(primaryCfg);
    
    // 重置 planeHandle / 屏幕范围
    uiRenderer->resetPlaneHandle(primaryPlaneHandle);
    uiRenderer->resetTargetSize(screenSize); // 屏幕原始范围

    // 重置视频采集
    auto ccfg = VisionPipeline::defaultCameraConfig(autoWidth, autoHeight);
    if (!vision_) { vision_   = std::make_unique<VisionPipeline>(ccfg); vision_->start(); }
    else          vision_->resetConfig(ccfg); // 会自动暂停, 再自启动
    vision_->resume();
    uiRenderer->resume();
    if (yoloProcessor) yoloProcessor->resume();
    refreshing.resume();
}

// ------------------ 信号绑定 ------------------
void AppContriller::Impl::signalBind() {
    if (!uiRenderer || !uiRenderer.get()) {
        std::cerr << "[AppContriller][ERROR] Failed to signalBind because UIrenderer not exist." << std::endl;
        throw std::runtime_error("AppContriller error when signalBind.\n");
    }
    // 绑定信号的引用
    const MainInterface *ui_ = uiRenderer->getCurrentWidgetUnsafe();
    // RGA有数据时(开启推理)
    vision_->registerOnRGA([this](DmaBufferPtr rgb, std::shared_ptr<void> hd){
        static int frameId{0};
        if (++frameId % 2 != 0) return;
        yoloProcessor->submit(std::move(rgb), hd);
        frameId=0;
    });
    // 推理结果交给Qt绘制
    yoloProcessor->setOnResult([this](object_detect_result_list ret){
        uiRenderer->updateBoxs(std::move(ret));
    });
    // 录像
    QObject::connect(ui_, &MainInterface::recordSignal, [this](bool status){
        if (refreshing.is_paused()) return;
        if (status) vision_->tryRecord(VisionPipeline::RecordStatus::Start);
        else        vision_->tryRecord(VisionPipeline::RecordStatus::Stop);
    });
    // 拍照
    QObject::connect(ui_, &MainInterface::photoSignal, [this] {
        if (refreshing.is_paused()) return;
        if (!vision_->tryCapture()){
            std::cerr << "[AppContriller][ERROR] Failed to capture photo." << std::endl;
        }
    });
    // 图像格式转换
    QObject::connect(ui_, &MainInterface::modelModeChange, [this](MainInterface::ModelMode mode) {
        if (refreshing.is_paused()) return;
        bool ret=true;
        switch (mode) {
        case MainInterface::ModelMode::Run:
            ret = vision_->setModelRunningStatus(VisionPipeline::ModelStatus::Start);
            if(yoloProcessor) yoloProcessor->resume();
            break;
        case MainInterface::ModelMode::Stop:
            ret = vision_->setModelRunningStatus(VisionPipeline::ModelStatus::Stop);
            if(yoloProcessor) yoloProcessor->pause();
            uiRenderer->updateBoxs(object_detect_result_list{});
            break;
        default:
            std::cerr << "[AppContriller][ERROR] Unknow ModelMode status." << std::endl;
            break;
        } 
        if (!ret) std::cerr << "[AppContriller][ERROR] VisionPipeline failed to set model status." << std::endl;
    });
    // 镜像
    QObject::connect(ui_, &MainInterface::mirrorModeChanged, [this](MainInterface::MirrorMode mode){
        if (refreshing.is_paused()) return;
        bool horizontal, vertical;
        switch (mode) {
        case MainInterface::MirrorMode::Normal:
            horizontal = false;
            vertical = false;
            break;
        case MainInterface::MirrorMode::Both:
            horizontal = true;
            vertical = true;
            break;
        case MainInterface::MirrorMode::Horizontal:
            horizontal = true;
            vertical = false;
            break;
        case MainInterface::MirrorMode::Vertical:
            horizontal = false;
            vertical = true;
            break;
        default:
            std::cerr << "[AppContriller][ERROR] Unknow MirrorMode." << std::endl;
            break;
        }
        vision_->setMirrorMode(horizontal, vertical);
    });
    // 曝光度
    QObject::connect(ui_, &MainInterface::exposureChanged, [this](float percentage) {      
        if (refreshing.is_paused()) return;
        vision_->setExposurePercentage(percentage);
    });
    // 置信度
    QObject::connect(ui_, &MainInterface::confidenceChanged, [this](float percentage) {
        if (refreshing.is_paused()) return;
        yoloProcessor->setThresh(percentage / 100);
    });
}

void AppContriller::Impl::loop() {
    std::vector<DmaBufferPtr> buffers;
    FramePtr frame;
    while(running){
        refreshing.wait_if_paused();
        if(!running) break;

        frame.reset();
        if (!vision_ || !vision_->getCurrentRawFrame(frame) || !frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        buffers.clear();

        auto Y = frame->sharedState(0)->dmabuf_ptr;
        auto UV = DmaBuffer::importFromFD(
            Y->fd(),
            Y->width(),
            Y->height() / 2,
            Y->format(),
            Y->pitch() * Y->height() / 2,
            Y->pitch() * Y->height()
        );
        if(!Y || !UV) continue;
        buffers.emplace_back(std::move(Y));
        buffers.emplace_back(std::move(UV));
        if (!overlayPlaneHandle.valid() || !running) continue;
        display_->presentFrame(overlayPlaneHandle, buffers, frame);
    }
}

// ------------------ 启动 ------------------
void AppContriller::Impl::start() {
    // 初始化时调用一次
    postProcess();
    // 重置标志位
    running.store(true);
    refreshing.resume();
    yoloProcessor->start();
    // 初始化推迟到启动
    uiRenderer->init();
    // 绑定信号与槽
    signalBind();
    // 启动显示线程
    display_->start();
    // 创建视频显示线程
    mainLoop = std::thread(std::bind(&Impl::loop, this));
    uiRenderer->setFPSUpdater([this](void) -> float {
        return vision_->getFPS();
    });
    // 启动UI渲染线程
    uiRenderer->start();
}

void AppContriller::Impl::quit() {
    if (!running.exchange(false)) return;
    std::cout << "[AppContriller] stopping..." << std::endl;
    running.store(false);
    if (mainLoop.joinable()) mainLoop.join();
    if(yoloProcessor){
        yoloProcessor->pause();
        yoloProcessor->start();
    }

    vision_->stop();
    display_->stop();
    uiRenderer->stop();
}

// ------------------ 对外接口 ------------------
AppContriller::AppContriller() : impl_(std::make_unique<Impl>() ){ }
AppContriller::~AppContriller(){ }
void AppContriller::start() { impl_->start(); }
void AppContriller::quit() { return impl_->quit(); }

#include "appController.h"
#include "threadUtils.h"
#include "fenceWatcher.h"

#include "displayManager.h"
#include "visionPipeline.h"

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

DisplayManager::PlaneHandle overlayPlaneHandle;
std::unique_ptr<VisionPipeline> vision_;
std::shared_ptr<DisplayManager> display_;
std::atomic<uint32_t> autoWidth, autoHeight;
std::atomic_bool running{true};             // 视频线程运行标志
std::atomic_bool refreshing{false};	        // 刷新标志

// ------------------ 资源回收 / 重置资源 ------------------
void preProcess(){
    refreshing.store(true);
}

void postProcess(){
    // 未初始化
    if (!display_) {
        std::cerr << "[AppContriller][ERROR] DisplayManager not init!" << std::endl;
        return;
    }

    // 获取合适的分辨率
    auto screenSize = display_->getCurrentScreenSize();
    if (screenSize.first <= 0 || screenSize.second <= 0) return;
    auto _ = chooseClosestResolution(screenSize.first, screenSize.second);
    autoWidth = _.first; autoHeight = _.second;

    // 重新获取PlaneHandle
    DisplayManager::PlaneConfig overlayCfg = {
        .type = DisplayManager::PlaneType::OVERLAY,
        .srcWidth = autoWidth,
        .srcHeight = autoHeight,
        .zOrder = 1
    };
    overlayPlaneHandle = display_->createPlane(overlayCfg);
    
    // 重置视频采集
    auto ccfg = VisionPipeline::defaultCameraConfig(autoWidth, autoHeight);
    if (!vision_) { vision_   = std::make_unique<VisionPipeline>(ccfg); vision_->start(); }
    else          vision_->resetConfig(ccfg); // 自动暂停
    refreshing.store(false);
}

void quit() {
    if (!running.exchange(false)) return;
    std::cout << "stopping..." << std::endl;
    running.store(false);
}

// 定时器线程函数 - 5秒后退出
void timerThreadFunc(int seconds) {
    std::cout << "Timer expired after " << seconds << " seconds, quitting..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    quit();
}

// ------------------ 启动 ------------------
void exec() {
    ThreadUtils::bindCurrentThreadToCore(2);

    // 重置标志位
    running.store(true);
    refreshing.store(false);
    
    // 启动显示线程
    display_->start();
    
    // // 启动定时器线程 - 5秒后退出
    // std::thread timerThread(timerThreadFunc, 10);
    // timerThread.detach();  // 分离线程

    std::vector<DmaBufferPtr> buffers;
    FramePtr frame;
    while(running){
        if (refreshing || !vision_){
            std::this_thread::yield();
            if(!running) break;
            continue;
        }

        if (!vision_->getCurrentRawFrame(frame) || !frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (!overlayPlaneHandle.valid()) continue;
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
        display_->presentFrame(overlayPlaneHandle, buffers, frame);
    }
}

int main(int argc, char const *argv[]) {
    // 创建全局唯一fd_ptr
    DrmDev::fd_ptr = DeviceController::create();
    // 退出信号处理
    std::signal(SIGINT, [](int signal) {
        if (signal == SIGINT) {
            std::cout << "Received SIGINT signal, exiting..." << std::endl;
            quit();
        }
    });

    display_  = std::make_shared<DisplayManager>();

    // 注册回调
    display_->registerPostRefreshCallback(std::bind(&postProcess));
    display_->registerPreRefreshCallback(std::bind(&preProcess));
    // 初始化时调用一次
    postProcess();

    exec();
    display_.reset();
    vision_.reset();
    return 0;
}

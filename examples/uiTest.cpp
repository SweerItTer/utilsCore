/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-09 17:41:22
 * @FilePath: /EdgeVision/examples/uiTest.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "displayManager.h"
#include "uiRenderer.h"
#include "rga/formatTool.h"

#include <vector>
#include <queue>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <random>
#include <thread>
#include <chrono>
#include <mutex>

#include <QApplication>

static std::atomic_bool running{true};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    DrmDev::fd_ptr = DeviceController::create();
    if (!DrmDev::fd_ptr || DrmDev::fd_ptr->get() < 0) return -1;
    static QApplication* app_ = &app;

    DisplayManager::PlaneHandle primaryPlaneHandle;
    std::atomic<uint32_t> screenWidth, screenHeight;

    // drm displayer 实例
    std::shared_ptr<DisplayManager> dm = std::make_shared<DisplayManager>();
    // ui renderer 实例
    std::shared_ptr<UIRenderer> uir = std::make_shared<UIRenderer>();

    std::signal(SIGINT, [] (int signal) {
        if (signal == SIGINT) {
            std::cout << "Ctrl+C received, stopping..." << std::endl;
            running.store(false);
            app_->quit();
        }
    });

    uir->bindDisplayer(dm);
    uir->loadCursorIcon("./cursor-64.png");
    
    auto post = [&]{
        auto screenSize = dm->getCurrentScreenSize();
        screenWidth  = screenSize.first;
        screenHeight = screenSize.second;
        
        DisplayManager::PlaneConfig primaryCfg {
            .type = DisplayManager::PlaneType::PRIMARY,
            .srcWidth = std::min<uint32_t>(screenWidth, 1920),
            .srcHeight = std::min<uint32_t>(screenHeight, 1080),
            .dstWidth = screenWidth,
            .dstHeight = screenHeight,
            .drmFormat = DRM_FORMAT_ABGR8888, /// RGBA
            .zOrder = 1
        };
        primaryPlaneHandle = dm->createPlane(primaryCfg);
        std::cout << "[Main] primaryPlaneHandle valid: " << primaryPlaneHandle.valid() << std::endl;
        std::cout << "[Main] Resolution: " << screenWidth << "x" << screenHeight << std::endl;

        uir->resetScreenSize(screenSize);
        uir->resetPlaneHandle(primaryPlaneHandle);
        uir->resume();
    };

    dm->registerPreRefreshCallback([&]{
        if (!running) return;
        if(uir) uir->pause(true);
    });
    
    dm->registerPostRefreshCallback(post);
    post();

    dm->start();
    while (!primaryPlaneHandle.valid()) ;
    uir->init();
    uir->start();

    // QTimer::singleShot(4000, [&]() {
    //     std::cout << "4-second timeout reached, exiting..." << std::endl;
    //     running.store(false);
    //     app_->quit();
    // });

    // 主线程堵塞
    app.exec();
    
    uir->stop();
    uir.reset();

    dm->stop();
    dm.reset();

    // std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    std::cout << "[Main] Program Exit." << std::endl;
    return 0;
}
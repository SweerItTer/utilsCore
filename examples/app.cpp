/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-15 00:37:18
 * @FilePath: /EdgeVision/examples/app.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <QApplication>
#include <QTimer>
#include <csignal>
#include <iostream>
#include <chrono>
#include <thread>
#include "appController.h"
#include "displayManager.h" // 确保包含 DisplayManager

// 一个简单的等待函数，不依赖 Qt
void waitForScreenReady() {
    std::cout << "[Main] Waiting for HDMI/Screen connection..." << std::endl;
    
    // 创建一个临时的 DisplayManager 来检测
    auto tempDm = std::make_shared<DisplayManager>();
    tempDm->start(); 

    while (true) {
        // 检查是否有有效平面/连接
        if (tempDm->valid()) {
            auto size = tempDm->getCurrentScreenSize();
            if (size.first > 0 && size.second > 0) {
                std::cout << "[Main] Screen detected: " 
                          << size.first << "x" << size.second << std::endl;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "." << std::flush;
    }
    tempDm->stop();
    // 释放资源, 让后续 AppContriller 重新接管
    tempDm.reset(); 
}

int main(int argc, char *argv[]) {
    // 初始化 DRM fd
    DrmDev::fd_ptr = DeviceController::create();
    
    // 在启动 Qt 之前, 先堵塞等待屏幕就绪
    waitForScreenReady();

    QApplication app(argc, argv);
    AppContriller contriller;
    
    // 静态实例指针
    static QApplication* appPtr = &app;
    static AppContriller* contrillerPtr = &contriller;

    // 退出信号处理
    std::signal(SIGINT, [](int signal) {
        if (signal == SIGINT && contrillerPtr && appPtr) {
            std::cout << "Received SIGINT signal, exiting..." << std::endl;
            contrillerPtr->quit();
            appPtr->quit();
        }
    });

    // 10秒后自动退出的定时器
    // QTimer::singleShot(20000, [&app, &contriller]() {
    //     std::cout << "20-second timeout reached, exiting..." << std::endl;
    //     contriller.quit();
    //     app.quit();
    // });

    // 显示超时信息
    std::cout << "Application will exit automatically after 10 seconds..." << std::endl;
    std::cout << "Press Ctrl+C to exit immediately." << std::endl;

    contriller.start();
    
    return app.exec();
}
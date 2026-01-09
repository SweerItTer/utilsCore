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

int main(int argc, char *argv[]) {
    // 创建全局唯一fd_ptr
    DrmDev::fd_ptr = DeviceController::create();
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
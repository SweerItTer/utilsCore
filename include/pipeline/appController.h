/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:57:21
 * @FilePath: /EdgeVision/include/pipeline/appController.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <sys/syscall.h>
#include <csignal>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <thread>

#include "drm/deviceController.h"

class AppContriller {
private:
    /* data */
public:
    AppContriller();
    ~AppContriller();

    void start();
    void quit();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
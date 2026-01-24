/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-04 19:08:10
 * @FilePath: /include/utils/sys/cpuMonitor.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>

/* 用法:
 *      CpuMonitor cpu_monitor;
 *      float cpu_usage = cpu_monitor.getCpuUsage();
*/
#include "base.h"
#include <sstream>

class CpuMonitor : public ResourceMonitor {
private:
    unsigned long long last_total_ = 0;
    unsigned long long last_idle_ = 0;
    
    // 读取CPU状态
    bool readProcStat(unsigned long long& total, unsigned long long& idle) {
        std::ifstream file("/proc/stat");
        if (!file.is_open()) {
            std::cerr << "Failed to open /proc/stat\n";
            return false;
        }
        std::string line;
        std::getline(file, line);
        file.close();

        if (line.empty() || line.find("cpu ") != 0) {
            std::cerr << "Invalid /proc/stat format\n";
            return false;
        }
        /* 从 line 读出数据
         *      user  nice system idle_val iowait irq softirq steal guest guest_nice
         * cpu  13140 1    8758   5711247  5149   0   1891    0     0     0
         */
        std::istringstream iss(line);
        std::string cpu;
        unsigned long long user, nice, system, idle_val, iowait, irq, softirq, steal;
        iss >> cpu >> user >> nice >> system >> idle_val >> iowait >> irq >> softirq >> steal;
        // CPU 总时间
        total = user + nice + system + idle_val + iowait + irq + softirq + steal;
        // 空闲时间
        idle = idle_val; // 不含 iowait, 与 top 一致
        return true;
    }

    bool sampleUsage(float& usage) override {
        unsigned long long total, idle;
        if (!readProcStat(total, idle)) {
            return false;
        }

        if (last_total_ > 0) {
            unsigned long long delta_total = total - last_total_;
            unsigned long long delta_idle = idle - last_idle_;
            if (delta_total > 0) {
                usage = 100.0f * (delta_total - delta_idle) / delta_total;
                last_total_ = total;
                last_idle_ = idle;
                return true;
            }
        }

        last_total_ = total;
        last_idle_ = idle;
        return false; // 首次采样无有效数据
    }

public:
    CpuMonitor(int sleeptime = 1000) : ResourceMonitor(sleeptime, "/tmp/cpu_usage") {}
};
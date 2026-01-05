/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-05 14:23:16
 * @FilePath: /EdgeVision/examples/CpuMonitor.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <unistd.h>

class CpuMonitor {
public:
    using Clock = std::chrono::steady_clock;

    bool begin();
    bool end();
    double cpuUsagePercent() const;

private:
    uint64_t utimeBegin = 0;
    uint64_t stimeBegin = 0;
    uint64_t utimeEnd   = 0;
    uint64_t stimeEnd   = 0;

    Clock::time_point tpBegin;
    Clock::time_point tpEnd;
};

static bool readProcStat(uint64_t& utime, uint64_t& stime) {
    std::ifstream ifs("/proc/self/stat");
    if (false == ifs.is_open()) {
        return false;
    }

    std::string tmp;
    for (int i = 0; i < 13; ++i) {
        ifs >> tmp;
    }

    ifs >> utime >> stime;
    return true;
}

bool CpuMonitor::begin() {
    if (false == readProcStat(utimeBegin, stimeBegin)) {
        return false;
    }
    tpBegin = Clock::now();
    return true;
}

bool CpuMonitor::end() {
    if (false == readProcStat(utimeEnd, stimeEnd)) {
        return false;
    }
    tpEnd = Clock::now();
    return true;
}

double CpuMonitor::cpuUsagePercent() const {
    auto cpuTicks =
        (utimeEnd + stimeEnd) -
        (utimeBegin + stimeBegin);

    auto wallMs =
        std::chrono::duration<double, std::milli>(
            tpEnd - tpBegin
        ).count();

    long hz = sysconf(_SC_CLK_TCK);

    double cpuMs = cpuTicks * 1000.0 / hz;
    return cpuMs / wallMs * 100.0;
}
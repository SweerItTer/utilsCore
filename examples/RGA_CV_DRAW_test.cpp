/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-05 17:29:30
 * @Description: 性能对比测试程序：RGA 硬件加速 vs OpenCV CPU 实现
 */
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

#include "opencv_impl.h"
#include "rga_impl.h"
#include "CpuMonitor.h"
#include "CsvWriter.h"

// 测试配置
const int ITERATIONS = 500;    // 循环次数
const int SRC_W = 1920;        // 输入分辨率
const int SRC_H = 1080;
const int DST_W = 640;         // 目标分辨率 (如 YOLO 输入)
const int DST_H = 640;
const char PAD_COLOR = 114;    // 填充颜色

/**
 * @brief 计算 Letterbox 所需的矩形区域 (用于 RGA 核心函数)
 */
void calculate_letterbox_rects(int sw, int sh, int dw, int dh, rect& s_box, rect& d_box) {
    float scale = std::min((float)dw / sw, (float)dh / sh);
    int rw = (int)(sw * scale + 0.5f);
    int rh = (int)(sh * scale + 0.5f);
    
    // RGA 对齐要求
    int aw = (rw / 4) * 4;
    int ah = (rh / 2) * 2;

    int lp = (dw - aw) / 2;
    int tp = (dh - ah) / 2;

    s_box = {0, 0, sw - 1, sh - 1};
    d_box = {lp, tp, lp + aw - 1, tp + ah - 1};
}

int main(int argc, char const *argv[]) {
    // 1. 初始化硬件与环境
    DrmDev::fd_ptr = DeviceController::create("/dev/dri/card0");
    const int TEST_FRAMES = 500;
    
    cv::Mat src_mat(SRC_H, SRC_W, CV_8UC3, cv::Scalar(114, 114, 114));
    auto src_dma = DmaBuffer::create(SRC_W, SRC_H, DRM_FORMAT_RGB888, 0, 0); 
    auto dst_dma = DmaBuffer::create(DST_W, DST_H, DRM_FORMAT_RGB888, 0, 0); 
    
    rect s_box, d_box;
    calculate_letterbox_rects(SRC_W, SRC_H, DST_W, DST_H, s_box, d_box);

    CpuMonitor monitor;
    CsvWriter detail_csv("performance_analysis_independent.csv");
    detail_csv.writeHeader({"Frame_ID", "Type", "Latency_ms", "CPU_Percent"});

    // ============================================================
    // 单元 1: 独立测试 OpenCV (CPU)
    // ============================================================
    std::cout << "\n[Unit 1] Starting Independent OpenCV Profiling..." << std::endl;
    for (int i = 0; i < TEST_FRAMES; ++i) {
        monitor.begin();
        auto start = std::chrono::steady_clock::now();
        
        cv::Mat res = opencv_letterbox(src_mat, DST_W, DST_H, 114);
        
        auto end = std::chrono::steady_clock::now();
        monitor.end();
        
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        detail_csv.writeRow<std::string>({std::to_string(i), "OpenCV", std::to_string(ms), std::to_string(monitor.cpuUsagePercent())});
        
        if (i % 100 == 0) std::cout << "OpenCV processed " << i << " frames..." << std::endl;
    }

    // 冷却 2 秒，让 CPU 频率恢复正常，减少残留干扰
    std::cout << "Cooling down for 2 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // ============================================================
    // 单元 2: 独立测试 RGA (Hardware)
    // ============================================================
    std::cout << "\n[Unit 2] Starting Independent RGA Profiling..." << std::endl;
    // 预热：让驱动和硬件进入活跃状态
    for(int k=0; k<20; ++k) rga_process_core(src_dma, dst_dma, &s_box, &d_box, 114);

    for (int i = 0; i < TEST_FRAMES; ++i) {
        monitor.begin();
        auto start = std::chrono::steady_clock::now();
        
        rga_process_core(src_dma, dst_dma, &s_box, &d_box, 114);
        
        auto end = std::chrono::steady_clock::now();
        monitor.end();
        
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        detail_csv.writeRow<std::string>({std::to_string(i), "RGA", std::to_string(ms), std::to_string(monitor.cpuUsagePercent())});
        
        if (i % 100 == 0) std::cout << "RGA processed " << i << " frames..." << std::endl;
    }

    std::cout << "\nIndependent testing complete! Results saved." << std::endl;
    return 0;
}
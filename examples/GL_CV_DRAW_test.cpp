#include <QApplication>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <thread>
#include <cmath>

#include "drm/deviceController.h"
#include "dma/dmaBuffer.h"
#include "rander/draw.h"
#include "fenceWatcher.h"

// ======================== 测试配置 ========================
constexpr int TEST_LOOP_COUNT = 300;
constexpr int WARMUP_LOOPS = 20; 

struct Boxs4Test {
    int x, y, w, h;
};

std::vector<Boxs4Test> boxs = {
    {100, 100, 200, 200}, {400, 300, 150, 250}, {800, 600, 300, 100},
    {1200, 800, 400, 400}, {1600, 1200, 500, 300}, {2000, 1000, 250, 350},
    {2500, 1500, 180, 220}, {3000, 800, 320, 180}, {3500, 2000, 280, 320},
    {100, 1800, 420, 280}
};

struct TestResult {
    double averageTime = 0;
    double minTime = 0;
    double maxTime = 0;
    double p95Time = 0;
    double p99Time = 0;
    double stdDev = 0;
    std::vector<double> timings; 
};

// 工具函数：计算统计信息
TestResult calculateStats(std::vector<double> timings) {
    if (timings.empty()) return {};
    TestResult res;
    res.timings = timings;
    std::sort(timings.begin(), timings.end());
    
    double sum = std::accumulate(timings.begin(), timings.end(), 0.0);
    res.averageTime = sum / timings.size();
    res.minTime = timings.front();
    res.maxTime = timings.back();
    res.p95Time = timings[static_cast<size_t>(timings.size() * 0.95)];
    res.p99Time = timings[static_cast<size_t>(timings.size() * 0.99)];
    
    double sq_sum = 0;
    for(double t : timings) sq_sum += (t - res.averageTime) * (t - res.averageTime);
    res.stdDev = std::sqrt(sq_sum / timings.size());
    
    return res;
}

// ======================== GPU 测试函数 ========================
TestResult GPUDrawTest(TestResult& hwResultOut) {
    std::cout << "[Step 1] Initializing GPU Resources..." << std::endl;
    Core& core = Core::instance();
    Draw& draw = Draw::instance();

    const std::string slotType = "test_slot";
    DmaBufferPtr dmabufTemplate = DmaBuffer::create(3840, 2160, DRM_FORMAT_ABGR8888, 0, 0);
    core.registerResSlot(slotType, 30, std::move(dmabufTemplate));
        
    std::vector<DrawBox> drawBoxes;
    for (const auto& box : boxs) {
        drawBoxes.emplace_back(QRectF(box.x, box.y, box.w, box.h), QColor(255, 0, 0, 255), "");
    }
        
    core.makeQCurrent();
    
    // --- 预热阶段 ---
    std::cout << "[Step 2] Warming up (20 loops)..." << std::endl;
    for(int i=0; i < WARMUP_LOOPS; ++i) {
        auto slot = core.acquireFreeSlot(slotType);
        draw.clear(*slot, Qt::black); // 确保内部已优化为 glClear
        int f = -1; 
        slot->syncToDmaBuf(f);
        glFinish(); 
        if(f >= 0) close(f); 
        core.releaseSlot(slotType, slot);
    }

    // --- 准备测量工具 ---
    GLuint queries[TEST_LOOP_COUNT];
    glGenQueries(TEST_LOOP_COUNT, queries);
    std::vector<double> cpuSubmitTimings;
    cpuSubmitTimings.reserve(TEST_LOOP_COUNT);

    std::cout << "[Step 3] Running GPU Benchmarking..." << std::endl;
    for (int i = 0; i < TEST_LOOP_COUNT; ++i) {
        auto slot = core.acquireFreeSlot(slotType); 
        auto start = std::chrono::high_resolution_clock::now();
        
        // 开启硬件计时
        glBeginQuery(GL_TIME_ELAPSED_EXT, queries[i]);

        draw.clear(*slot, QColor(0, 0, 0, 255));
        draw.drawText(*slot, "GPU Performance Test", QPointF(100, 100), Qt::green, 48);
        draw.drawBoxes(*slot, drawBoxes, 3);
        
        // 结束硬件计时
        glEndQuery(GL_TIME_ELAPSED_EXT);

        int fence = -1;
        slot->syncToDmaBuf(fence); 

        // 异步回收
        FenceWatcher::instance().watchFence(fence, [&slot, slotType, &core](){
            core.releaseSlot(slotType, slot);
        });

        auto end = std::chrono::high_resolution_clock::now();
        cpuSubmitTimings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    // 收集 GPU 硬件样本
    glFinish(); 
    std::vector<double> hwTimings;
    for (int i = 0; i < TEST_LOOP_COUNT; ++i) {
        GLuint timeElapsed = 0;
        glGetQueryObjectuiv(queries[i], GL_QUERY_RESULT, &timeElapsed);
        hwTimings.push_back(timeElapsed / 1000000.0);
    }
    glDeleteQueries(TEST_LOOP_COUNT, queries);
    core.doneQCurrent();

    hwResultOut = calculateStats(hwTimings);
    return calculateStats(cpuSubmitTimings);
}

// ======================== OpenCV 测试函数 ========================
TestResult OpenCVDrawTest() {
    std::cout << "[Step 4] Running OpenCV Benchmarking..." << std::endl;
    cv::Mat img(2160, 3840, CV_8UC4, cv::Scalar(0,0,0,255));
    std::vector<double> timings;
    timings.reserve(TEST_LOOP_COUNT);

    for (int i = 0; i < TEST_LOOP_COUNT; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        img.setTo(cv::Scalar(0, 0, 0, 255));
        cv::putText(img, "OpenCV Performance Test", cv::Point(100, 100), 
                    cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(0, 255, 0), 3);
        for (const auto& box : boxs) {
            cv::rectangle(img, cv::Point(box.x, box.y), 
                         cv::Point(box.x + box.w, box.y + box.h), 
                         cv::Scalar(0, 0, 255), 3);
        }
        auto end = std::chrono::high_resolution_clock::now();
        timings.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    return calculateStats(timings);
}

// ======================== 数据输出 (ASCII & CSV) ========================
void saveAndPrintAll(const TestResult& gpuSub, const TestResult& gpuHw, const TestResult& cvRes) {
    // 1. ASCII 表格输出
    std::cout << "\n" << std::string(105, '=') << "\n";
    std::cout << " GPU (零拷贝+异步) vs CPU (OpenCV) 多维度性能报告\n";
    std::cout << std::string(105, '=') << "\n";
    printf("| %-18s | %-18s | %-18s | %-18s | %-15s |\n", 
           "指标 (ms)", "GPU Submit(CPU)", "GPU Hardware", "OpenCV(CPU)", "提升(HW vs CV)");
    std::cout << std::string(105, '-') << "\n";

    auto printRow = [&](std::string name, double v1, double v2, double v3) {
        double speedup = (v3 - v2) / v3 * 100.0;
        printf("| %-18s | %-18.3f | %-18.3f | %-18.3f | %-14.1f%% |\n", 
               name.c_str(), v1, v2, v3, speedup);
    };

    printRow("Average", gpuSub.averageTime, gpuHw.averageTime, cvRes.averageTime);
    printRow("Min", gpuSub.minTime, gpuHw.minTime, cvRes.minTime);
    printRow("Max", gpuSub.maxTime, gpuHw.maxTime, cvRes.maxTime);
    printRow("P95", gpuSub.p95Time, gpuHw.p95Time, cvRes.p95Time);
    printRow("P99", gpuSub.p99Time, gpuHw.p99Time, cvRes.p99Time);
    printRow("StdDev", gpuSub.stdDev, gpuHw.stdDev, cvRes.stdDev);
    std::cout << std::string(105, '=') << "\n";

    // 2. CSV 文件保存
    std::ofstream csv("full_performance_report.csv");
    csv << "Frame,GPUSubmit_ms,GPUHardware_ms,OpenCV_ms\n";
    for(int i=0; i<TEST_LOOP_COUNT; ++i) {
        csv << i+1 << "," << gpuSub.timings[i] << "," 
            << gpuHw.timings[i] << "," << cvRes.timings[i] << "\n";
    }
    csv.close();
    std::cout << ">> 详细样本已保存至: full_performance_report.csv" << std::endl;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    DrmDev::fd_ptr = DeviceController::create();
    if (!DrmDev::fd_ptr) return -1;

    TestResult gpuHwResult;
    TestResult gpuSubmitResult = GPUDrawTest(gpuHwResult);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    TestResult cvResult = OpenCVDrawTest();

    saveAndPrintAll(gpuSubmitResult, gpuHwResult, cvResult);

    Draw::instance().shutdown();
    Core::instance().shutdown();
    return 0;
}
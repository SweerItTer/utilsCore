#include "visionPipeline.h"
#include "CpuMonitor.h"
#include "LatencyStats.h"
#include "CsvWriter.h"
#include "drm/deviceController.h"

#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <atomic>
#include <csignal>
#include <iomanip> // Required for std::setprecision

using Clock = std::chrono::steady_clock;

// =============== 全局退出标志 ===============
static std::atomic<bool> g_shouldExit{false};

void signalHandler(int signum) {
    std::cout << "\n[Signal] Received signal " << signum << ", exiting..." << std::endl;
    g_shouldExit.store(true);
}

// =============== 配置常量 ===============
struct BenchmarkConfig {
    static constexpr size_t WARMUP_FRAMES = 30;
    static constexpr size_t SAMPLE_FRAMES = 200;
    
    static const std::vector<std::pair<uint32_t, uint32_t>>& testResolutions() {
        static const std::vector<std::pair<uint32_t, uint32_t>> resolutions = {
            {3840, 2160},  // 4K
            {1920, 1080},  // 1080p
            {1280, 720},   // 720p
            {640, 480}     // VGA
        };
        return resolutions;
    }
};

// =============== 测试上下文 ===============
struct TestContext {
    LatencyStats latency;
    CpuMonitor cpu;
    CsvWriter summaryCSV;
    CsvWriter detailCSV;
    
    size_t frameCount{0};
    size_t resolutionIndex{0};
    size_t globalFrameIndex{0};
    Clock::time_point lastTimestamp;
    
    std::atomic<bool> testCompleted{false};  // 测试完成标志
    
    explicit TestContext(const std::string& summaryPath, const std::string& detailPath)
        : summaryCSV(summaryPath)
        , detailCSV(detailPath)
    {
        summaryCSV.writeHeader({
            "width", "height",
            "mean_ms", "min_ms", "max_ms", "stddev_ms",
            "cpu_percent"
        });
        
        detailCSV.writeHeader({
            "global_frame",
            "resolution_frame",
            "width",
            "height",
            "interval_ms",
            "phase"
        });
    }
    
    void reset() {
        latency.reset();
        frameCount = 0;
    }
    
    bool isFirstFrame() const {
        return frameCount == 1;
    }
    
    bool isWarmupPhase() const {
        return frameCount <= BenchmarkConfig::WARMUP_FRAMES;
    }
    
    bool isSampleComplete() const {
        return latency.count() == BenchmarkConfig::SAMPLE_FRAMES;
    }
    
    bool hasMoreResolutions() const {
        return resolutionIndex < BenchmarkConfig::testResolutions().size();
    }
    
    std::string getCurrentPhase() const {
        if (frameCount == 1) return "init";
        if (isWarmupPhase()) return "warmup";
        return "sampling";
    }
};

// =============== 核心逻辑 ===============
class LatencyBenchmark {
public:
    LatencyBenchmark()
        : context_("latency_summary.csv", "latency_detail.csv")
    {
        initializeDrm();
        initializePipeline();
    }
    
    ~LatencyBenchmark() {
        cleanup();
    }
    
    void run() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "   Frame Latency Benchmark" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Warmup frames: " << BenchmarkConfig::WARMUP_FRAMES << std::endl;
        std::cout << "Sample frames: " << BenchmarkConfig::SAMPLE_FRAMES << std::endl;
        std::cout << "Output files:" << std::endl;
        std::cout << "  - latency_summary.csv (统计汇总)" << std::endl;
        std::cout << "  - latency_detail.csv  (每帧详情)" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        pipeline_->start();
        
        // 等待测试完成或收到退出信号
        while (!g_shouldExit.load() && !context_.testCompleted.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        cleanup();
    }

private:
    TestContext context_;
    std::shared_ptr<VisionPipeline> pipeline_;
    CameraController::Config currentConfig_;
    bool cleaned_{false};
    
    void initializeDrm() {
        DrmDev::fd_ptr = DeviceController::create("/dev/dri/card0");
    }
    
    void initializePipeline() {
        const auto& resolutions = BenchmarkConfig::testResolutions();
        auto width = resolutions[0].first;
        auto height = resolutions[0].second; 
        
        currentConfig_ = VisionPipeline::defaultCameraConfig(
            width, height, V4L2_PIX_FMT_NV12
        );
        
        pipeline_ = std::make_shared<VisionPipeline>(currentConfig_);
        
        pipeline_->registerOnFrameReady([this](FramePtr frame) {
            onFrameReady(frame);
        });
    }
    
    void onFrameReady(FramePtr frame) {
        // 检查退出信号
        if (g_shouldExit.load()) {
            return;
        }
        
        context_.frameCount++;
        context_.globalFrameIndex++;
        
        if (context_.isFirstFrame()) {
            handleFirstFrame();
            return;
        }
        
        auto now = Clock::now();
        double intervalMs = calculateInterval(now);
        
        recordFrameDetail(intervalMs);
        context_.lastTimestamp = now;
        
        if (context_.isWarmupPhase()) {
            return;
        }
        
        context_.latency.add(intervalMs);
        
        if (context_.latency.count() % 50 == 0) {
            printProgress(intervalMs);
        }
        
        if (context_.isSampleComplete()) {
            finishCurrentResolution();
        }
    }
    
    void handleFirstFrame() {
        context_.lastTimestamp = Clock::now();
        context_.cpu.begin();
        
        const auto& resolutions = BenchmarkConfig::testResolutions();
        auto width = resolutions[context_.resolutionIndex].first;
        auto height = resolutions[context_.resolutionIndex].second;
        
        std::cout << "[Benchmark] Testing " << width << "x" << height 
                  << " - Started" << std::endl;
    }
    
    double calculateInterval(Clock::time_point now) {
        return std::chrono::duration<double, std::milli>(
            now - context_.lastTimestamp
        ).count();
    }
    
    void recordFrameDetail(double intervalMs) {
        const auto& resolutions = BenchmarkConfig::testResolutions();
        auto width = resolutions[context_.resolutionIndex].first;
        auto height = resolutions[context_.resolutionIndex].second;
        
        context_.detailCSV.writeRow<std::string>({
            std::to_string(context_.globalFrameIndex),
            std::to_string(context_.frameCount),
            std::to_string(width),
            std::to_string(height),
            std::to_string(intervalMs),
            context_.getCurrentPhase()
        });
    }
    
    void printProgress(double currentIntervalMs) {
        std::cout << "  Progress: " << context_.latency.count() 
                  << "/" << BenchmarkConfig::SAMPLE_FRAMES 
                  << " frames"
                  << " | Current: " << std::fixed << std::setprecision(2) 
                  << currentIntervalMs << "ms"
                  << " | Avg: " << context_.latency.mean() << "ms"
                  << std::endl;
    }
    
    void finishCurrentResolution() {
        context_.cpu.end();
        
        const auto& resolutions = BenchmarkConfig::testResolutions();
        auto width = resolutions[context_.resolutionIndex].first;
        auto height = resolutions[context_.resolutionIndex].second;
        
        context_.summaryCSV.writeRow<double>({
            static_cast<double>(width),
            static_cast<double>(height),
            context_.latency.mean(),
            context_.latency.min(),
            context_.latency.max(),
            context_.latency.stddev(),
            context_.cpu.cpuUsagePercent()
        });
        
        printResolutionSummary(width, height);
        
        context_.resolutionIndex++;
        
        if (!context_.hasMoreResolutions()) {
            finishBenchmark();
            return;
        }
        
        switchToNextResolution();
    }
    
    void printResolutionSummary(uint32_t width, uint32_t height) {
        std::cout << "\n----------------------------------------" << std::endl;
        std::cout << "✓ Completed: " << width << "x" << height << std::endl;
        std::cout << "  Mean:   " << std::fixed << std::setprecision(3) 
                  << context_.latency.mean() << " ms" << std::endl;
        std::cout << "  Min:    " << context_.latency.min() << " ms" << std::endl;
        std::cout << "  Max:    " << context_.latency.max() << " ms" << std::endl;
        std::cout << "  StdDev: " << context_.latency.stddev() << " ms" << std::endl;
        std::cout << "  CPU:    " << std::fixed << std::setprecision(1)
                  << context_.cpu.cpuUsagePercent() << " %" << std::endl;
        std::cout << "----------------------------------------\n" << std::endl;
    }
    
    void switchToNextResolution() {
        context_.reset();
        
        const auto& resolutions = BenchmarkConfig::testResolutions();
        auto width = resolutions[context_.resolutionIndex].first;
        auto height = resolutions[context_.resolutionIndex].second;
        
        std::cout << "[Benchmark] Switching to " 
                  << width << "x" << height << "..." << std::endl;
        
        currentConfig_.width = width;
        currentConfig_.height = height;
        
        // 先暂停再重置，减少竞态条件
        pipeline_->pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        pipeline_->resetConfig(currentConfig_);
        
        std::cout << "  Waiting for pipeline stabilization..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 恢复运行
        pipeline_->resume();
    }
    
    void finishBenchmark() {
        std::cout << "\n========================================"  << std::endl;
        std::cout << "   Benchmark Completed Successfully!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total frames processed: " << context_.globalFrameIndex << std::endl;
        std::cout << "\nResults saved to:" << std::endl;
        std::cout << "  📊 latency_summary.csv - Statistical summary" << std::endl;
        std::cout << "  📈 latency_detail.csv  - Frame-by-frame data" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
        context_.testCompleted.store(true);
    }
    
    void cleanup() {
        if (cleaned_) return;
        cleaned_ = true;
        
        std::cout << "[Benchmark] Cleaning up..." << std::endl;
        
        if (pipeline_) {
            pipeline_->stop();
            // 等待 pipeline 完全停止
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            pipeline_.reset();
        }
        
        std::cout << "[Benchmark] Cleanup completed." << std::endl;
    }
};

// =============== 主函数 ===============
int main(int argc, char** argv) {
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        LatencyBenchmark benchmark;
        benchmark.run();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
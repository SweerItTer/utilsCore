/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-07 00:18:54
 * @FilePath: /EdgeVision/examples/pipelineTest.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "visionPipeline.h"
#include <csignal>
static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running.store(false);
    }
}

int test(VisionPipeline& vp){
    
    using clock_t = std::chrono::steady_clock;

    auto t_record_start = clock_t::now();
    auto t_capture_start = clock_t::now();

    bool record1_done = false;
    bool record2_done = false;

    std::cout << "[Main] Start recording #1 (30s)" << std::endl;
    vp.tryRecord(VisionPipeline::RecordStatus::Start);

    while (running) {
        auto now = clock_t::now();

        // 每10秒拍照一次
        // auto capture_sec = std::chrono::duration_cast<std::chrono::seconds>(now - t_capture_start).count();
        // if (capture_sec >= 2) {
        //     if (!vp.tryCapture()) std::cerr << "[Main]: tryCapture Failed." << std::endl;
        //     else std::cout << "[Main]: Capture photo." << std::endl;
        //     t_capture_start = now;
        // }

        // 第一段录像 30s
        if (!record1_done) {
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - t_record_start).count();
            if (sec >= 5) {
                std::cout << "[Main] Stop recording #1" << std::endl;
                vp.tryRecord(VisionPipeline::RecordStatus::Stop);
                record1_done = true;
                t_record_start = clock_t::now(); // 复用为间隔计时

                // dfconfig.width = 1920; dfconfig.height = 1080;
                // vp.resetConfig(dfconfig);
            }
        }
        // 关闭 10s
        else if (record1_done && !record2_done) {
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - t_record_start).count();
            if (sec >= 2) {
                std::cout << "[Main] Start recording #2 (30s)" << std::endl;
                vp.tryRecord(VisionPipeline::RecordStatus::Start);
                record2_done = true;
                t_record_start = clock_t::now();  // 第二段录像计时
                t_capture_start = clock_t::now(); // 重置拍照计时
            }
        }
        // 第二段录像 30s
        else {
            auto sec = std::chrono::duration_cast<std::chrono::seconds>(now - t_record_start).count();
            if (sec >= 10) {
                vp.tryRecord(VisionPipeline::RecordStatus::Stop);
                break;
            }
        }
        usleep(33000); // 30fps 主循环节流
    }
    return 0;
}

int main() {
    DrmDev::fd_ptr = DeviceController::create(); // 初始化全局唯一fd_ptr

    std::signal(SIGINT, handleSignal);

    auto dfconfig = VisionPipeline::defaultCameraConfig(3840, 2160);
    VisionPipeline vp(dfconfig);

    vp.start();
    for(int i=3; i>0; --i){
        if (!running) break;
        test(vp);
    }

    std::cout << "[Main] Program Exit" << std::endl;
    return 0;
}

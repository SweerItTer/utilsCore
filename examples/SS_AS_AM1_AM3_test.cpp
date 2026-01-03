#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <queue>

#include <opencv2/opencv.hpp>

#include "yolov5s.h"
#include "rknnPool.h"
#include "drm/deviceController.h"

// =======================
// 单帧统计结果
// =======================
struct BenchResult {
    std::string modeName;
    double avgFps;
    double totalTime;
    int frames;
    std::vector<double> samples;
};

// =======================
// Tester
// =======================
class ExtremeTester {
public:
    // -----------------------
    // Mode 1: Sync
    // -----------------------
    BenchResult runSync(
        const std::string& video,
        const std::string& model,
        int total_f) {

        Yolov5s yolo(model, "./coco_80_labels_list.txt");
        yolo.init(yolo.getCurrentContext(), false);

        cv::VideoCapture cap(video, cv::CAP_ANY);
        if (!cap.isOpened()) {
            std::cerr << "[ERROR] Cannot open video: " << video << std::endl;
            return {};
        }

        std::vector<double> samples;
        samples.reserve(total_f);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < total_f; ++i) {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) break;

            DmaBufferPtr buf =
                DmaBuffer::create(frame.cols, frame.rows,
                                  DRM_FORMAT_RGB888, 0);

            uint8_t* dst = static_cast<uint8_t*>(buf->map());
            int stride = buf->pitch();

            for (int y = 0; y < frame.rows; ++y) {
                memcpy(dst + y * stride,
                       frame.data + y * frame.cols * 3,
                       frame.cols * 3);
            }
            buf->unmap();

            auto t1 = std::chrono::high_resolution_clock::now();
            yolo.infer(buf);
            auto t2 = std::chrono::high_resolution_clock::now();

            samples.push_back(
                std::chrono::duration<double, std::milli>(t2 - t1).count()
            );
        }

        auto end = std::chrono::high_resolution_clock::now();
        double duration =
            std::chrono::duration<double>(end - start).count();

        return {
            "同步单线程",
            total_f / duration,
            duration,
            total_f,
            samples
        };
    }

    // -----------------------
    // Async (size=1 / size=3)
    // -----------------------
    BenchResult runAsync(
        const std::string& name,
        const std::string& video,
        const std::string& model,
        int pool_size,
        int target_f) {
    
        rknnPool<Yolov5s, DmaBufferPtr, object_detect_result_list> pool(
            model, "./coco_80_labels_list.txt", pool_size);
        pool.init();
    
        cv::VideoCapture cap(video, cv::CAP_ANY);
        if (!cap.isOpened()) {
            std::cerr << "[ERROR] Cannot open video: " << video << std::endl;
            return {};
        }
    
        std::vector<double> samples;
        samples.reserve(target_f);
    
        std::atomic<int> put_cnt{0};
        std::atomic<int> get_cnt{0};
        std::atomic<bool> producer_done{false};
    
        std::queue<std::chrono::high_resolution_clock::time_point> time_fifo;
        std::mutex fifo_mutex;
    
        auto bench_start = std::chrono::high_resolution_clock::now();
    
        // Producer
        std::thread producer([&]() {
            while (put_cnt < target_f) {
                cv::Mat frame;
                if (!cap.read(frame)) break;
    
                DmaBufferPtr buf =
                    DmaBuffer::create(frame.cols, frame.rows,
                                      DRM_FORMAT_RGB888, 0);
    
                uint8_t* dst = static_cast<uint8_t*>(buf->map());
                int stride = buf->pitch();
    
                for (int y = 0; y < frame.rows; ++y) {
                    memcpy(dst + y * stride,
                           frame.data + y * frame.cols * 3,
                           frame.cols * 3);
                }
                buf->unmap();
    
                if (pool.put(buf) == 0) {
                    auto now = std::chrono::high_resolution_clock::now();
                    {
                        std::lock_guard<std::mutex> lk(fifo_mutex);
                        time_fifo.push(now);
                    }
    
                    int cur = ++put_cnt;
                    if (cur % 50 == 0) {
                        std::cout << "\r[" << name << "] put "
                                  << cur << std::flush;
                    }
                }
            }
            producer_done.store(true);
        });
    
        // Consumer
        while (!producer_done || get_cnt < put_cnt) {
            object_detect_result_list out;
            if (pool.get(out, 0) == 0) {
                auto now = std::chrono::high_resolution_clock::now();
    
                std::chrono::high_resolution_clock::time_point t0;
                {
                    std::lock_guard<std::mutex> lk(fifo_mutex);
                    if (time_fifo.empty()) continue;
                    t0 = time_fifo.front();
                    time_fifo.pop();
                }
    
                samples.push_back(
                    std::chrono::duration<double, std::milli>(now - t0).count()
                );
                ++get_cnt;
            }
        }
    
        producer.join();
    
        auto bench_end = std::chrono::high_resolution_clock::now();
        double duration =
            std::chrono::duration<double>(bench_end - bench_start).count();
    
        int frames = samples.size();
    
        return {
            name,
            frames / duration,
            duration,
            frames,
            samples
        };
    }
    

    // -----------------------
    // Report & CSV (UNCHANGED)
    // -----------------------
    void printReport(const std::vector<BenchResult>& results) {
        std::cout << "\n"
                  << std::string(70, '=') << "\n"
                  << "              RK356x NPU Benchmark Result\n"
                  << std::string(70, '=') << "\n";

        std::cout << std::left
                  << std::setw(25) << "Mode"
                  << std::setw(10) << "FPS"
                  << std::setw(15) << "Avg Lat(ms)"
                  << std::setw(12) << "Total(s)"
                  << "Frames\n";

        std::cout << std::string(70, '-') << "\n";

        for (const auto& r : results) {
            double avg_lat =
                std::accumulate(r.samples.begin(),
                                r.samples.end(), 0.0) / r.frames;

            std::cout << std::left
                      << std::setw(25) << r.modeName
                      << std::setw(10) << std::fixed << std::setprecision(1) << r.avgFps
                      << std::setw(15) << avg_lat
                      << std::setw(12) << r.totalTime
                      << r.frames << "\n";
        }

        std::cout << std::string(70, '=') << "\n";
    }

    void saveSamples(const std::vector<BenchResult>& results) {
        std::ofstream f("npu_benchmark_samples.csv");
        f << "frame_index";
        for (auto& r : results) f << "," << r.modeName;
        f << "\n";

        for (int i = 0; i < results[0].frames; ++i) {
            f << i;
            for (auto& r : results) f << "," << r.samples[i];
            f << "\n";
        }

        std::cout << "[Info] CSV exported\n";
    }
};

// =======================
// main
// =======================
int main() {
    DrmDev::fd_ptr = DeviceController::create();
    std::cout << "Init DeviceController success\n";

    ExtremeTester tester;

    std::string video_path = "/model/test.avi";
    std::string model_path = "./yolov5s_relu.rknn";
    int test_frames = 222;

    std::vector<BenchResult> results;

    std::cout << "Running sync test...\n";
    results.push_back(
        tester.runSync(video_path, model_path, test_frames));
    std::cout << "Running async tests...\n";
    results.push_back(
        tester.runAsync("异步单线程(size=1)",
                        video_path, model_path, 1, test_frames));
    std::cout << "Running async tests (size=3)...\n";
    results.push_back(
        tester.runAsync("异步多线程(size=3)",
                        video_path, model_path, 3, test_frames));

    tester.printReport(results);
    tester.saveSamples(results);

    return 0;
}

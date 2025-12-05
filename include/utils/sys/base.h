/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-17 02:50:41
 * @FilePath: /EdgeVision/include/utils/sys/base.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <string>

class ResourceMonitor {
protected:
    // 最新负载(0.0 - 100.0 或其他单位)
    std::atomic<float> usage_{0.0f};
    // 线程标志
    std::atomic<bool> running_{true};
    std::atomic<bool> pause_{false};
    // 锁/条件变量
    std::mutex mutex_;
    std::condition_variable cv_;
    // 时间节点
    std::atomic<std::chrono::steady_clock::time_point> last_access_{std::chrono::steady_clock::now()};
    // 监控线程
    std::thread monitor_thread_;
    // 轮询间隔(毫秒)
    int sleeptime_;
    // 输出文件(如 /tmp/cpu_usage)
    std::string output_file_; 

    // 虚函数: 子类实现具体的采样逻辑
    virtual bool sampleUsage(float& usage) = 0;

    void monitorLoop() {
        const std::chrono::milliseconds poll_interval{sleeptime_};
        const std::chrono::seconds pause_timeout{30};

        while (running_) {
            // 检查是否需要暂停
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto now = std::chrono::steady_clock::now();
                if (now - last_access_.load() > pause_timeout && !pause_) {
                    pause_ = true;
                    std::cerr << "Pausing monitor thread for " << output_file_ << "\n";
                }
                if (pause_) {
                    cv_.wait(lock, [this] { return !pause_ || !running_; });
                    if (!running_) break;
                }
            }

            // 更新数据
            float usage;
            if (sampleUsage(usage)) {
                usage_ = usage;
                std::ofstream out(output_file_);
                if (out.is_open()) {
                    out << std::fixed << std::setprecision(1) << usage << "\n";
                    out.close();
                }
            }

            std::this_thread::sleep_for(poll_interval);
        }
    }

public:
    ResourceMonitor(int sleeptime, const std::string& output_file)
        : sleeptime_(sleeptime), output_file_(output_file) {
        // 启动监控线程
        monitor_thread_ = std::thread(&ResourceMonitor::monitorLoop, this);
    }

    virtual ~ResourceMonitor() {
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pause_ = false;
            cv_.notify_all();
        }
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    float getUsage() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 更新调用时间
        last_access_ = std::chrono::steady_clock::now();
        // 唤醒线程
        if (pause_) {
            pause_ = false;
            cv_.notify_one();
        }
        return usage_;
    }
};
/*
 * @FilePath: /examples/threadpool_scale_demo.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: asyncThreadPool 自动扩/缩容演示
 */

#include "asyncThreadPool.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

static size_t runBurst(asyncThreadPool& pool, int tasks, int sleepMs) {
    std::mutex mtx;
    std::unordered_set<std::thread::id> workers;
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(tasks));

    for (int i = 0; i < tasks; ++i) {
        futures.emplace_back(pool.enqueue([&] {
            {
                std::lock_guard<std::mutex> lock(mtx);
                workers.insert(std::this_thread::get_id());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }));
    }

    for (auto& f : futures) f.wait();
    return workers.size();
}

int main() {
    asyncThreadPool pool(1, 8, 4096);

    std::printf("=== asyncThreadPool scale demo ===\n");
    std::printf("aliveThreads(init) = %zu\n", pool.aliveThreadCount());

    // Burst1 must last longer than MANAGERINTERVAL (5s) so the manager thread has a chance to expand.
    const auto t1 = std::chrono::steady_clock::now();
    const int burst1Tasks = 128;
    const int burst1SleepMs = 120; // ~15s with 1 thread, but should shrink with expansion

    std::printf("[Burst1] submit tasks=%d sleepMs=%d\n", burst1Tasks, burst1SleepMs);

    std::mutex mtx;
    std::unordered_set<std::thread::id> workers;
    std::vector<std::future<void>> futures;
    futures.reserve(static_cast<size_t>(burst1Tasks));
    for (int i = 0; i < burst1Tasks; ++i) {
        futures.emplace_back(pool.enqueue([&] {
            {
                std::lock_guard<std::mutex> lock(mtx);
                workers.insert(std::this_thread::get_id());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(burst1SleepMs));
        }));
    }

    // Observe expansion while tasks are still running.
    for (int sec = 0; sec < 8; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::printf("aliveThreads(t+%ds) = %zu\n", sec + 1, pool.aliveThreadCount());
    }

    for (auto& f : futures) f.wait();
    const auto t2 = std::chrono::steady_clock::now();

    std::printf("[Burst1] unique worker threads seen = %zu\n", workers.size());
    std::printf("[Burst1] elapsed = %lld ms\n",
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
    std::printf("aliveThreads(after burst1) = %zu\n", pool.aliveThreadCount());

    std::printf("\nWaiting 8s to allow shrink...\n");
    std::this_thread::sleep_for(std::chrono::seconds(8));
    std::printf("aliveThreads(after idle) = %zu\n", pool.aliveThreadCount());

    const size_t burst2Workers = runBurst(pool, 2, 30);
    std::printf("[Burst2] unique worker threads seen = %zu\n", burst2Workers);
    std::printf("aliveThreads(after burst2) = %zu\n", pool.aliveThreadCount());

    return 0;
}

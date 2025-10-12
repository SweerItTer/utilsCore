/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 01:18:14
 * @FilePath: /EdgeVision/src/utils/asyncThreadPool.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "asyncThreadPool.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <iostream>

asyncThreadPool::asyncThreadPool(std::size_t poolSize, std::size_t maxQueueSize)
    : running_(true), maxQueueSize_(maxQueueSize)
{
    // poolSize 检查
    auto maxAllowedSize = static_cast<size_t>(std::thread::hardware_concurrency());
    if (poolSize > maxAllowedSize){
        poolSize = maxAllowedSize;
    } else if ( poolSize <= 0 ) {
        poolSize = 1;
    }
    fprintf(stdout, "Real pool size:%d\n", poolSize);
    // 创建线程池
    for (std::size_t i = 0; i < poolSize; ++i) {
        workers_.emplace_back([this]{ worker(); });
    }
}

asyncThreadPool::~asyncThreadPool()
{
    running_ = false;
    queueNotEmpty_.notify_all();
    queueNotFull_.notify_all();
    for (auto& t : workers_) if (t.joinable()) t.join();
}

void asyncThreadPool::worker()
{
    fprintf(stdout, "ThreadPool worker TID: %d \n", syscall(SYS_gettid));
    while (running_ || !tasks_.empty()) {
        std::function<void()> task;
        std::unique_lock<std::mutex> lock(queueMutex_);
        // 短暂释放锁, 等待任务入队唤醒 (notify_one/notify_all)
        auto notEmpty = queueNotEmpty_.wait_for(lock, std::chrono::milliseconds(10), [this]{ 
            // 队列不空 → 马上醒, 线程池关闭 → 马上醒, 否则继续等（最多 10ms 超时）
            return !tasks_.empty() || !running_; });
        
        if (!notEmpty) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue; // 超时回到循环，避免 CPU 长时间堵塞
        }
        if (!running_) break;
        task = std::move(tasks_.front());
        tasks_.pop();
        // 运行入队
        queueNotFull_.notify_one();

        lock.unlock();

        if (task) {
            task();
        }
    }
}

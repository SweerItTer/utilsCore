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
    : running_(true), maxQueueSize_(maxQueueSize) {
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
    stop();
}

void asyncThreadPool::stop() {
    running_ = false;
    condition_.notify_all();  // 唤醒所有等待的线程

    for (auto& t : workers_) if (t.joinable()) t.join();
}

void asyncThreadPool::worker()
{
    fprintf(stdout, "ThreadPool worker TID: %d \n", syscall(SYS_gettid));
    std::function<void()> task;

    while (running_) {
        // 队列为空时等待
        std::unique_lock<std::mutex> lock(queue_mutex_);
        condition_.wait(lock, [this] {
            // 被唤醒后检查: 有新任务或线程池关闭
            return tasks_.size_approx() > 0 || !running_;
        });
        if (tasks_.try_dequeue(task)) {
            if (!running_) break;
            if (task) task();
        }
    }
    // 清空剩余任务
    while (tasks_.size_approx() > 0)
        tasks_.try_dequeue(task);
}

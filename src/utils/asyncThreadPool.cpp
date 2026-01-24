/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 01:18:14
 * @FilePath: /src/utils/asyncThreadPool.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "asyncThreadPool.h"
#include <iostream>
#include <algorithm>

/**
 * @brief 封装线程信息
 */
struct asyncThreadPool::WorkerWrapper {
    std::thread thread;
    std::atomic<bool> stopFlag{false};
    std::chrono::steady_clock::time_point lastActiveTime;
};

// ---------------- 构造/析构 ----------------
asyncThreadPool::asyncThreadPool(std::size_t minThreads, std::size_t maxThreads, std::size_t maxQueueSize)
    : running_(true), minThreads_(minThreads), maxThreads_(maxThreads), maxQueueSize_(maxQueueSize)
{
    if(minThreads_ <= 0) minThreads_ = 1;
    if(maxThreads_ < minThreads_) maxThreads_ = static_cast<size_t>(std::thread::hardware_concurrency());

    // 启动最小线程数
    for(size_t i = 0; i < minThreads_; ++i){
        auto wrapper = std::make_shared<WorkerWrapper>();
        workers_.push_back(wrapper);
        wrapper->thread = std::thread([this, wrapper]{ worker(wrapper); });
    }

    // 启动管理线程
    managerThread_ = std::thread([this]{ managerThreadFunc(); });
}

asyncThreadPool::~asyncThreadPool() {
    stop();
}

// ----------------- worker -----------------
void asyncThreadPool::worker(std::shared_ptr<WorkerWrapper> wrapper) {
    wrapper->lastActiveTime = std::chrono::steady_clock::now();

    while(running_ && !wrapper->stopFlag){
        std::function<void ()> task;
        {
            std::unique_lock<std::mutex> lock(queueMtx_);
            condition_.wait_for(lock, std::chrono::milliseconds(100),
                [this]{ return !tasks_.size_approx() || !running_; });
            if (!running_ || wrapper->stopFlag) break;
            if(!tasks_.size_approx()){
                tasks_.try_dequeue(task);
            } else {
                continue;
            }
        }

        activeTasks_++;
        wrapper->lastActiveTime = std::chrono::steady_clock::now();
        try { task(); }
        catch(...){ }//std::cerr<<"[ThreadPool] Task exception\n"; }
        activeTasks_--;

        queueNotFullCv_.notify_one();
    }
}

// ----------------- manager -----------------
void asyncThreadPool::managerThreadFunc() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queueMtx_);
        // 条件变量等待：任务入队通知 或 managerInterval 超时
        managerCv_.wait_for(lock, MANAGERINTERVAL, [this]{
            return !running_ || tasks_.size_approx() > 0;
        });
        lock.unlock();
        if (!running_) break;
        adjustThreads();
    }
}

void asyncThreadPool::adjustThreads() {
    std::lock_guard<std::mutex> lock(queueMtx_);
    size_t taskCount = tasks_.size_approx();
    size_t totalWorkers = workers_.size();
    size_t idleWorkers = 0;

    auto now = std::chrono::steady_clock::now();
    for(auto &w : workers_){
        if(std::chrono::duration_cast<std::chrono::seconds>(now - w->lastActiveTime).count() > 2){
            idleWorkers++;
        }
    }

    // 扩容
    if(taskCount > 0 && totalWorkers < maxThreads_){
        size_t addCount = std::min(taskCount, maxThreads_ - totalWorkers);
        for(size_t i=0;i<addCount;++i){
            auto wrapper = std::make_shared<WorkerWrapper>();
            workers_.push_back(wrapper);
            wrapper->thread = std::thread([this, wrapper]{ worker(wrapper); });
        }
    }

    // 收缩
    if(idleWorkers > taskCount && totalWorkers > minThreads_){
        size_t removeCount = std::min(idleWorkers - taskCount, totalWorkers - minThreads_);
        for(auto &w : workers_){
            if(removeCount == 0) break;
            if(std::chrono::duration_cast<std::chrono::seconds>(now - w->lastActiveTime).count() > 2){
                w->stopFlag = true;
                removeCount--;
            }
        }

        // 清理已停止线程
        workers_.erase(
            std::remove_if(workers_.begin(), workers_.end(),
                [](auto &w){
                    if(w->stopFlag && w->thread.joinable()){
                        w->thread.join();
                        return true;
                    }
                    return false;
                }),
            workers_.end());
    }
}

// ----------------- 停止线程池 -----------------
void asyncThreadPool::stop() {
    if(!running_.exchange(false)) return;
    condition_.notify_all();
    queueNotFullCv_.notify_all();
    managerCv_.notify_one();
    // 停止管理线程
    if(managerThread_.joinable())
        managerThread_.join();

    // 停止工作线程
    for(auto &w : workers_){
        w->stopFlag = true;
        if(w->thread.joinable()) w->thread.join();
    }
    workers_.clear();
}

/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 01:18:14
 * @FilePath: /src/utils/asyncThreadPool.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "asyncThreadPool.h"
#include "logger_v2.h"
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
void asyncThreadPool::worker(std::weak_ptr<WorkerWrapper> wrapperWeak) {
    auto wrapper = wrapperWeak.lock();
    if (!wrapper) return;

    {
        std::lock_guard<std::mutex> lock(queueMtx_);
        wrapper->lastActiveTime = std::chrono::steady_clock::now();
    }

    while (running_.load(std::memory_order_relaxed) && !wrapper->stopFlag.load(std::memory_order_relaxed)) {
        std::function<void ()> task;
        {
            std::unique_lock<std::mutex> lock(queueMtx_);
            workerCv_.wait(lock, [this, &wrapper] {
                return !running_.load(std::memory_order_relaxed) ||
                       wrapper->stopFlag.load(std::memory_order_relaxed) ||
                       tasks_.size_approx() > 0;
            });
            if (!running_.load(std::memory_order_relaxed) || wrapper->stopFlag.load(std::memory_order_relaxed)) break;
            if (!tasks_.try_dequeue(task)) continue;
            wrapper->lastActiveTime = std::chrono::steady_clock::now();
        }

        activeTasks_.fetch_add(1, std::memory_order_relaxed);
        try { task(); }
        catch(...){ LOG_ERROR("[ThreadPool] Task exception\n"); }
        activeTasks_.fetch_sub(1, std::memory_order_relaxed);

        queueNotFullCv_.notify_one();
    }
}

// ----------------- manager -----------------
void asyncThreadPool::managerThreadFunc() {
    while (running_) {
        std::this_thread::sleep_for(MANAGERINTERVAL);
        if (!running_) break;
        adjustThreads();
    }
}

void asyncThreadPool::adjustThreads() {
    static constexpr auto kIdleThreshold = std::chrono::seconds(2);

    // 只有 manager 调用
    // 重复使用避免反复申请内存
    thread_local std::vector<std::shared_ptr<WorkerWrapper>> toStart;
    thread_local std::vector<std::shared_ptr<WorkerWrapper>> toStop;
    thread_local std::vector<std::shared_ptr<WorkerWrapper>> idleCandidates;

    toStart.clear();
    toStop.clear();
    idleCandidates.clear();

    const auto now = std::chrono::steady_clock::now();

    auto idleFor = [now](const std::shared_ptr<WorkerWrapper>& w) {
        return now - w->lastActiveTime;
    };

    {
        std::lock_guard<std::mutex> lock(queueMtx_);

        // 当前待处理任务数量
        const uint64_t backlog = static_cast<uint64_t>(tasks_.size_approx());
        // 当前处理中任务数量
        const uint64_t active = static_cast<uint64_t>(activeTasks_.load(std::memory_order_relaxed));
        // 任务总数
        const uint64_t demand = backlog + active;

        // 当前线程总数
        const size_t totalWorkers = workers_.size();
        // 理论需求线程数
        const size_t desiredWorkers = static_cast<size_t>(
            std::min<uint64_t>(maxThreads_, std::max<uint64_t>(minThreads_, demand)));

        // 扩容
        if (desiredWorkers > totalWorkers) {
            const size_t addCount = desiredWorkers - totalWorkers;
            toStart.reserve(addCount);
            for (size_t i = 0; i < addCount; ++i) {
                auto wrapper = std::make_shared<WorkerWrapper>();
                wrapper->lastActiveTime = now;
                workers_.push_back(wrapper);
                toStart.push_back(std::move(wrapper));
            }
        }

        // 缩容
        if (desiredWorkers < totalWorkers && totalWorkers > minThreads_) {
            // 取前50%休眠线程
            const bool lowLoad = (demand * 2 <= totalWorkers);
            if (lowLoad) {
                const size_t maxRemovable = totalWorkers - minThreads_;
                const size_t wantRemove = std::min(totalWorkers - desiredWorkers, maxRemovable);

                if (wantRemove > 0) {
                    idleCandidates.reserve(totalWorkers);
                    for (const auto& w : workers_) {
                        if (!w->stopFlag.load(std::memory_order_relaxed) && idleFor(w) > kIdleThreshold) {
                            idleCandidates.push_back(w);
                        }
                    }

                    if (!idleCandidates.empty()) {
                        const size_t willRemove = std::min(wantRemove, idleCandidates.size());

                        if (willRemove < idleCandidates.size()) {
                            std::nth_element(
                                idleCandidates.begin(),
                                idleCandidates.begin() + static_cast<std::ptrdiff_t>(willRemove),
                                idleCandidates.end(),
                                [&](const auto& a, const auto& b) { return idleFor(a) > idleFor(b); });
                        } else {
                            std::sort(
                                idleCandidates.begin(),
                                idleCandidates.end(),
                                [&](const auto& a, const auto& b) { return idleFor(a) > idleFor(b); });
                        }

                        toStop.reserve(willRemove);
                        for (size_t i = 0; i < willRemove; ++i) {
                            auto& w = idleCandidates[i];
                            if (w->stopFlag.exchange(true, std::memory_order_relaxed)) continue;
                            toStop.push_back(w);
                        }

                        if (!toStop.empty()) {
                            workers_.erase(
                                std::remove_if(workers_.begin(), workers_.end(),
                                    [](const auto& w) { return w->stopFlag.load(std::memory_order_relaxed); }),
                                workers_.end());
                        }
                    }
                }
            }
        }
    }

    if (!toStop.empty()) workerCv_.notify_all();
    
    for (auto& w : toStop) {
        if (w->thread.joinable()) w->thread.join();
    }
    
    for (auto& w : toStart) {
        w->thread = std::thread([this, w] { worker(w); });
    }   
}

size_t asyncThreadPool::aliveThreadCount() const {
    std::lock_guard<std::mutex> lock(queueMtx_);
    size_t count = 0;
    for (const auto& w : workers_) {
        if (w && !w->stopFlag.load(std::memory_order_relaxed)) count++;
    }
    return count;
}

// ----------------- 停止线程池 -----------------
void asyncThreadPool::stop() {
    if(!running_.exchange(false)) return;
    workerCv_.notify_all();
    queueNotFullCv_.notify_all();
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

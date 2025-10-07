/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-05 19:23:07
 * @FilePath: /EdgeVision/include/utils/asyncThreadPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef ASYNC_THREAD_POOL_H
#define ASYNC_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <chrono>
#include <stdexcept>

class asyncThreadPool {
public:
    asyncThreadPool(std::size_t poolSize, std::size_t maxQueueSize = 16);

    ~asyncThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        // 获取返回值类型
        using resultType = typename std::result_of<F(Args...)>::type;
        // 创建task
        auto task = std::make_shared<std::packaged_task<resultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // 获取future
        std::future<resultType> task_future = task->get_future();

        // 线程池关闭
        if (false == running_){
            fprintf(stderr, "ThreadPool stopped\n");
            return task_future;
        }

        // 超时等待入队
        std::unique_lock<std::mutex> lock(queueMutex_);
        auto isQueue = queueNotFull_.wait_for(lock, std::chrono::milliseconds(10), [this] {
            return tasks_.size() < maxQueueSize_ || !running_; 
        });
        if ( false == isQueue ) {
            fprintf(stderr, "ThreadPool queue full or stopped\n");
            return task_future;
        }

        tasks_.emplace([task]{
            (*task)();
        });
        // 通知工作线程
        queueNotEmpty_.notify_one();
        return task_future;
    }
private:
    void worker();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable queueNotEmpty_;
    std::condition_variable queueNotFull_;
    std::atomic<bool> running_;
    std::size_t maxQueueSize_;
};
#endif

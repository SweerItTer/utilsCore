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

#include "concurrentqueue.h"

class asyncThreadPool{
public:
    asyncThreadPool(std::size_t poolSize, std::size_t maxQueueSize = 16);

    ~asyncThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>{
        auto execute = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        // 获取任务返回值
        using resultType = typename std::result_of<F(Args...)>::type;
        using PackagedTask = std::packaged_task<resultType()>;
        
        // packaged_task : 创建一个异步任务,并且在完成时调用回调函数 (resultType())
        auto task = std::make_shared<PackagedTask>(std::move(execute));

        // future 用于获取(等待)任务返回值
        std::future<resultType> res = task->get_future();

        if (false == running_) throw std::runtime_error("enqueue on stopped ThreadPool");
        if (tasks_.size_approx() > maxQueueSize_){
            return std::future<resultType>();  // 返回空 future
        }
        // 解引用给出可调用对象
        tasks_.enqueue([task]{
            (*task)();
        });
        condition_.notify_one();
        return res;
    }
private:
    void worker();
private:
    std::atomic<bool> running_;
    std::size_t poolSize_, maxQueueSize_;
    std::vector<std::thread> workers_;
    moodycamel::ConcurrentQueue<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
};
#endif

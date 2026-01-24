/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-05 19:23:07
 * @FilePath: /include/utils/asyncThreadPool.h
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
#include <memory>
#include <stdexcept>
#include "concurrentqueue.h"

// 管理线程检查间隔
static constexpr std::chrono::milliseconds MANAGERINTERVAL = std::chrono::milliseconds(5000);

/**
 * @brief 异步线程池类
 * 
 * 线程池管理一组工作线程, 可动态伸缩。
 * 支持任务优先级, 用户可选择阻塞或非阻塞入队。
 */
class asyncThreadPool {
public:

    /**
     * @brief 构造线程池
     * 
     * @param minThreads 最小线程数
     * @param maxThreads 最大线程数
     * @param maxQueueSize 队列最大长度
     */
    asyncThreadPool(std::size_t minThreads=-1, std::size_t maxThreads=-1, std::size_t maxQueueSize = 64);

    /**
     * @brief 析构函数, 停止线程池
     */
    ~asyncThreadPool();

    /**
     * @brief 阻塞入队任务（如果队列满, 会等待）
     * 
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 可调用对象参数
     * @return std::future<R> 返回任务执行结果的 future
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using resultType = typename std::result_of<F(Args...)>::type;
        auto taskPtr = std::make_shared<std::packaged_task<resultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<resultType> res = taskPtr->get_future();
        std::unique_lock<std::mutex> lock(queueMtx_);

        // 阻塞直到队列有空位
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });

        if(!running_) return std::future<resultType>();

        tasks_.enqueue([taskPtr]{ (*taskPtr)(); });
        condition_.notify_one();

        managerCv_.notify_one();
        return res;
    }

    /**
     * @brief 非阻塞入队任务（如果队列满, 会直接返回空 future）
     * 
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 可调用对象参数
     * @return std::future<R> 返回任务执行结果的 future, 如果入队失败, 则 future invalid
     */
    template<class F, class... Args>
    auto try_enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using resultType = typename std::result_of<F(Args...)>::type;
        auto taskPtr = std::make_shared<std::packaged_task<resultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<resultType> res = taskPtr->get_future();
        std::lock_guard<std::mutex> lock(queueMtx_);

        if(tasks_.size_approx() >= maxQueueSize_ || !running_)
            return std::future<resultType>(); // 队列满直接返回空 future

        tasks_.enqueue([taskPtr]{ (*taskPtr)(); });
        condition_.notify_one();

        managerCv_.notify_one();
        return res;
    }

    /**
     * @brief 手动停止线程池
     */
    void stop();

private:
    // ----------------- 内部类型 -----------------
    struct WorkerWrapper; // 工作线程封装, 记录活跃时间与停止标志

    // ----------------- 内部函数 -----------------
    void worker(std::shared_ptr<WorkerWrapper> wrapper);      // 工作线程函数
    void managerThreadFunc();                                 // 管理线程函数
    void adjustThreads();                                     // 动态扩缩容逻辑

    // ----------------- 数据成员 -----------------
    std::atomic<bool> running_;
    std::atomic<size_t> activeTasks_{0};                      // 当前活跃任务数

    std::mutex queueMtx_;
    std::condition_variable condition_;
    std::condition_variable queueNotFullCv_;
    std::condition_variable managerCv_;                       // 管理线程条件变量

    moodycamel::ConcurrentQueue<std::function<void()>> tasks_;                 // 任务队列
    std::size_t maxQueueSize_;

    std::thread managerThread_;                               // 管理线程
    std::vector<std::shared_ptr<WorkerWrapper>> workers_;

    size_t minThreads_;
    size_t maxThreads_;
};

#endif
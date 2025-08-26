/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 01:01:58
 * @FilePath: /EdgeVision/include/utils/asyncThreadPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef ASYNC_THREAD_POOL_H
#define ASYNC_THREAD_POOL_H

#include <memory>
#include <functional>
#include <future>
#include <queue>

class asyncThreadPool{
public:
    explicit asyncThreadPool(std::size_t poolSize);
    ~asyncThreadPool();
    void clear();

    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>{
        // 获取任务返回值
        using resultType = typename std::result_of<F(Args...)>::type;

        // packaged_task : 创建一个异步任务,并且在完成时调用回调函数 (resultType())
        auto task = std::make_shared<std::packaged_task<resultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // future 用于获取(等待)任务返回值
        std::future<resultType> res = task->get_future();
        {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (false == running) throw std::runtime_error("enqueue on stopped ThreadPool");
        // 解引用给出可调用对象
        tasks.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }
private:
    void worker();
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::atomic<bool> running;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
};
#endif

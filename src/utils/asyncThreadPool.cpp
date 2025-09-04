/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 01:18:14
 * @FilePath: /EdgeVision/src/utils/asyncThreadPool.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "asyncThreadPool.h"


asyncThreadPool::asyncThreadPool(std::size_t poolSize)
: running(true), poolSize_(poolSize)  {
    // 直接去最大可用线程数
    std::size_t worksSize = std::thread::hardware_concurrency();
    
    for (size_t i = 0; i < worksSize; i++)
    {
        workers.emplace_back([this](){
            worker();
        });
    }
    
}

void asyncThreadPool::worker()
{
    while (true)
    {
        std::function<void()> task;
        {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        // 短暂释放锁,等待唤醒 (notify_one/notify_all)
        condition_.wait(lock, [this] {
            // 被唤醒后检查 队列不为空或线程池正在关闭
            // 若为false继续等待
            return false == tasks.empty() || false == running;
        });
        // 提前结束
        if (true == tasks.empty() && false == running) return;
        task = std::move(tasks.front());
        tasks.pop();
        }

        try {
            task();
        } catch (const std::exception& e) {
            // 记录异常, 避免线程退出
            fprintf(stderr, "Worker task exception: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "Worker task unknown exception\n");
        }
    }
}

asyncThreadPool::~asyncThreadPool()
{
    clear();
    {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    running = false;
    }
    condition_.notify_all();
    for(auto& worker : workers){
        if (worker.joinable()) worker.join();
    }
}

void asyncThreadPool::clear()
{
    {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    while(!tasks.empty()) {
        tasks.pop();
    }
    }
    condition_.notify_all();
}
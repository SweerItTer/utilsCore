/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-27 02:35:47
 * @FilePath: /EdgeVision/include/utils/objectsPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>

template<typename T>
class ObjectPool {
public:
    using CreatorFunc = std::function<T()>;

    ObjectPool(size_t poolSize, CreatorFunc creator) : creator_(creator) {
        for (size_t i = 0; i < poolSize; ++i) {
            free_.push(creator_());
        }
    }

    // 获取一个空闲对象，如果没有则阻塞等待
    T acquire() { // 返回 T 而不是 T&&
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return !free_.empty(); });
        T obj = std::move(free_.front());
        free_.pop();
        return obj;
    }

    bool tryAcquire(T& obj, std::chrono::milliseconds timeout){
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cond_.wait_for(lock, timeout, [this]{ return !free_.empty(); })) {
            return false; // 超时
        }
        obj = std::move(free_.front());
        free_.pop();
        return true;
    }

    // 归还对象回池
    void release(T obj) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            free_.push(std::move(obj));
        }
        cond_.notify_one();
    }

    size_t freeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_.size();
    }

private:
    CreatorFunc creator_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::queue<T> free_;
};

#endif // OBJECT_POOL_H
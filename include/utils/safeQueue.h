/*
 * @FilePath: /EdgeVision/include/utils/safeQueue.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-05-18 19:49:05
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class SafeQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    size_t max_size_ = 0; // 0 表示无限制

public:
    explicit SafeQueue(size_t max_size = 0) : max_size_(max_size) {}
    // 禁止拷贝
    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    // 只允许移动入队
    // enqueue(std::move(item)) 避免歧义
    void enqueue(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (max_size_ > 0 && queue_.size() >= max_size_) {
            // 超过限制时可丢弃最旧的帧、丢弃新帧、或者阻塞，按需求自行定制
            // 这里示例：丢弃新帧
            return;
        }
        queue_.push(std::move(item));
        cond_.notify_one();
    }

    // 阻塞式出队
    T dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    // 非阻塞尝试出队
    bool try_dequeue(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
    }

    size_t size() {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

#endif // SAFE_QUEUE_H

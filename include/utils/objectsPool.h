/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-27 02:35:47
 * @FilePath: /EdgeVision/include/utils/objectsPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>

template<typename T>
class ObjectPool {
public:
    using Ptr = std::shared_ptr<T>;
    using CreatorFunc = std::function<Ptr()>;

    ObjectPool(size_t poolSize, CreatorFunc creator) : creator_(creator) {
        for (size_t i = 0; i < poolSize; ++i) {
            free_.push_back(creator_());
        }
    }

    // 获取一个空闲对象，如果没有则阻塞等待
    Ptr acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return !free_.empty(); });
        Ptr obj = free_.back();
        free_.pop_back();
        return obj;
    }

    // 归还对象回池
    void release(Ptr obj) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            free_.push_back(obj);
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
    std::vector<Ptr> free_;
};

/*
 * @FilePath: /EdgeVision/include/utils/safeQueue.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-05-18 19:49:05
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

/* 使用vector作为底层存储 循环队列 */
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <type_traits>

/* 在多模板的情况下,优先级为 全特化 - 偏特化 - 默认主模板
 * 对于该文件的实现,优先匹配 class SafeQueue<Ptr, typename std::enable_if<
        std::is_same<Ptr, std::shared_ptr<typename Ptr::element_type>>::value ||
        std::is_same<Ptr, std::unique_ptr<typename Ptr::element_type>>::value
    >::type>
 * 如果 std::enable_if<true> 将通过 ::type 返回 void 类型, 代表匹配成功
 * 若 enable_if<false>::type 匹配失败,被丢弃（SFINAE）
 * 所以不会因为我把主模板放在前面就会先去匹配它
 * 不会因为偏特化匹配成功,即:class SafeQueue<Ptr, void> 和 class SafeQueue<T, void>一致而导致使用主模板
 */

// 主模板声明
template <typename T, typename Enable = void>
class SafeQueue;

// 针对智能指针的特化版本
/* 不管是 shared_ptr 还是 unique_ptr ,都有 element_type
 * 通过 typename Ptr::element_type 获取源指针的类型 T (typename 告诉编译器这是个类型)
 * 通过 std::is_same 判断 Ptr 是否为 std::shared_ptr<T> 或 std::unique_ptr<T>
 * 当判断结果为 true , std::enable_if<true>::type 返回 void 类型
 */
template <typename Ptr>
class SafeQueue<Ptr, typename std::enable_if<
    std::is_same<Ptr, std::shared_ptr<typename Ptr::element_type>>::value ||
    std::is_same<Ptr, std::unique_ptr<typename Ptr::element_type>>::value
>::type> {
private:    
    std::vector<Ptr> buffer_;
    size_t head_ = 0;               // 队列头部索引
    size_t tail_ = 0;               // 队列尾部索引
    size_t size_ = 0;               // 当前队列大小
    size_t capacity_ = 0;           // 队列容量
    
    std::atomic<bool> shutdown_{false};  // 关闭标志(若在堵塞出队的情况下(且size_=0)析构将会导致析构卡死)
    mutable std::mutex mutex_;           // const 成员函数获取锁的时候需要修改mutex_(状态), 因此需要添加 mutable
    std::condition_variable not_empty_cond_;
    std::condition_variable not_full_cond_;
public:
    // 溢出策略枚举
    enum class OverflowPolicy {
        DISCARD_OLDEST,  // 丢弃最旧的项目
        DISCARD_NEWEST,  // 丢弃最新的项目
        BLOCK,           // 阻塞直到有空间
        THROW_EXCEPTION  // 抛出异常
    };
    
    explicit SafeQueue(size_t capacity, OverflowPolicy policy = OverflowPolicy::DISCARD_OLDEST) 
        : capacity_(capacity), policy_(policy) {
        buffer_.resize(capacity);
    }

    // 禁止拷贝
    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;
    
    // 允许移动
    SafeQueue(SafeQueue&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        buffer_ = std::move(other.buffer_);
        head_ = other.head_;
        tail_ = other.tail_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        policy_ = other.policy_;
        shutdown_.store(other.shutdown_.load());
        
        // 重置源对象状态
        other.head_ = 0;
        other.tail_ = 0;
        other.size_ = 0;
        other.capacity_ = 0;
        other.shutdown_.store(true);
    }
    
    SafeQueue& operator=(SafeQueue&& other) noexcept {
        if (this != &other) {
            /* 关联而不锁死,避免死锁
            // 直接对称死锁
            queueA = std::move(queueB);// 线程1
            queueB = std::move(queueA); // 线程2
            */
            std::unique_lock<std::mutex> lock1(mutex_, std::defer_lock);
            std::unique_lock<std::mutex> lock2(other.mutex_, std::defer_lock);
            // 同时锁住
            std::lock(lock1, lock2);

            buffer_ = std::move(other.buffer_);
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            policy_ = other.policy_;
            shutdown_.store(other.shutdown_.load());
            
            // 重置源对象状态
            other.head_ = 0;
            other.tail_ = 0;
            other.size_ = 0;
            other.capacity_ = 0;
            other.shutdown_.store(true);
        }
        return *this;
    }

    
    ~SafeQueue() {
        shutdown();
        clear();
    }
    void shutdown() {
        shutdown_.store(true);
        not_empty_cond_.notify_all();
        not_full_cond_.notify_all();
    }

    // 入队操作 - 现在接受对象的唯一所有权
    bool enqueue(Ptr&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (shutdown_.load()) {
            return false; // 已关闭，不允许入队
        }

        // 检查队列是否已满
        if (size_ >= capacity_) {
            switch (policy_) {
                case OverflowPolicy::DISCARD_OLDEST:
                    head_ = (head_ + 1) % capacity_;
                    --size_;
                    break;
                case OverflowPolicy::DISCARD_NEWEST:
                    return false;
                case OverflowPolicy::BLOCK:
                    while (size_ >= capacity_ && !shutdown_.load()) {
                        not_full_cond_.wait(lock);
                    }
                    if (shutdown_.load()) return false;
                    break;
                case OverflowPolicy::THROW_EXCEPTION:
                    throw std::runtime_error("Queue is full");
            }
        }
        
        buffer_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        ++size_;
        
        not_empty_cond_.notify_one();
        return true;
    }

    // 阻塞式出队 - 返回对象的唯一所有权
    Ptr dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (0 == size_ && !shutdown_.load()) {
            not_empty_cond_.wait(lock);
        }
        if (0 == size_ && shutdown_.load()) {
            return nullptr; // 返回空，表示队列关闭
        }

        Ptr item = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        
        not_full_cond_.notify_one();
        return item;
    }

    // 非阻塞尝试出队
    bool try_dequeue(Ptr& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (0 == size_) return false;

        item = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        --size_;
        
        not_full_cond_.notify_one();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : buffer_) {
            item.reset();
        }
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        not_full_cond_.notify_all();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }
private:
    OverflowPolicy policy_;
};

#endif // SAFE_QUEUE_H

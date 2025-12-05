#ifndef ORDERED_QUEUE_H
#define ORDERED_QUEUE_H 1

#include <atomic>
#include <vector>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <cstdio>
#include <cstdint>

/*
 * OrderedQueue - 高性能无锁环形缓冲有序队列(模板化)
 *
 * 特点：
 *  - 支持多生产者并发入队(lock-free CAS)
 *  - 支持单消费者或多消费者顺序出队
 *  - 使用环形缓冲 + slot CAS 避免 map 分配开销
 *  - slot 内存由外部管理，入队/出队只操作 slot.data_
 *  - 可选丢弃策略：DISCARD_OLDEST / DISCARD_NEWEST / BLOCK / THROW_EXCEPTION
 *  - 支持超时出队
 *  - 提供统计信息：总入队/出队、timeout、slot 冲突、pending
 *
 * 注意：
 *  - 容量建议为 2 的幂，方便快速索引计算
 *  - 高乱序入队可能导致 slot 冲突，需要根据 overflow policy 处理
 */

template <typename T>
class OrderedQueue {
public:
    // 容量超限或 slot 冲突处理策略
    enum class OverflowPolicy {
        DISCARD_OLDEST,  // 丢弃环形缓冲中旧的帧
        DISCARD_NEWEST,  // 丢弃当前入队帧
        BLOCK,           // 阻塞等待空槽
        THROW_EXCEPTION  // 抛异常
    };

private:
    // 环形缓冲槽
    struct BufferSlot {
        std::atomic<bool>* filled = nullptr; // true 表示 slot 已被占用
        uint64_t frame_id{0};            // 当前帧 id
        T data_;                         // 外部管理的帧数据
        BufferSlot(){
            filled = new std::atomic<bool>(false);
        }
        ~BufferSlot(){
            if (filled){
                delete filled;
                filled = nullptr;
            }
        }
    };

    size_t capacity_;                     // 环形缓冲容量(必须为 2 的幂)
    std::vector<BufferSlot> ring_buffer_; // 环形缓冲存储
    std::atomic<uint64_t> expected_id_{0}; // 消费者期望的下一个 frame_id

    // 统计信息(避免频繁锁操作)
    alignas(64) std::atomic<uint64_t> total_enqueued_{0};
    alignas(64) std::atomic<uint64_t> total_dequeued_{0};
    alignas(64) std::atomic<uint64_t> timeout_skip_count_{0};
    alignas(64) std::atomic<uint64_t> slot_conflict_count_{0};

    // 计算大于等于 n 的最小 2 的幂
    static size_t next_power_of_two(size_t n) {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
#if SIZE_MAX > UINT32_MAX
        n |= n >> 32;
#endif
        return n + 1;
    }

public:
    // 构造函数
    // capacity: 环形缓冲大小(最好大于最大乱序跨度)
    OrderedQueue(size_t capacity) {
        capacity_ = next_power_of_two(capacity);
        ring_buffer_.resize(capacity_);
    }

    OrderedQueue(const OrderedQueue&) = delete;
    OrderedQueue& operator=(const OrderedQueue&) = delete;

    // ===================== 核心接口 =====================

    /*
     * enqueue - 入队操作(支持多生产者并发)
     * frame_id: 当前帧 id
     * data: 帧数据 T(由外部管理)
     * policy: slot 冲突或容量超限时的处理策略
     * 返回 true 表示成功入队，false 表示被丢弃
     */
    bool enqueue(uint64_t frame_id, T&& data, OverflowPolicy policy = OverflowPolicy::DISCARD_NEWEST) {
        size_t idx = frame_id & (capacity_ - 1);  // 环形索引计算

        bool expected = false;
        if (frame_id < expected_id_.load(std::memory_order_relaxed)){
            // 过期数据
            return false;
        }
        while (!ring_buffer_[idx].filled->compare_exchange_weak(
            expected, true, std::memory_order_acq_rel))
        {
            // slot 已被占用 -> slot 冲突
            slot_conflict_count_.fetch_add(1, std::memory_order_relaxed);

            switch(policy) {
                case OverflowPolicy::DISCARD_NEWEST:
                    return false; // 丢弃当前帧
                case OverflowPolicy::DISCARD_OLDEST:
                    // 丢弃旧帧，腾出空间
                    ring_buffer_[idx].filled->store(false, std::memory_order_release);
                    expected = false;
                    break;
                case OverflowPolicy::BLOCK:
                    // 阻塞等待 slot 释放
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    expected = false;
                    break;
                case OverflowPolicy::THROW_EXCEPTION:
                    throw std::runtime_error("OrderedQueue slot conflict");
            }
        }

        // 写入数据
        ring_buffer_[idx].data_ = std::move(data);
        ring_buffer_[idx].frame_id = frame_id;
        total_enqueued_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /*
     * try_dequeue - 按 expected_id 顺序取出 slot.data_
     * data_out: 输出参数，成功时为有效 T
     * timeout_ms: 超时毫秒，0 表示非阻塞立即返回
     */
    bool try_dequeue(T& data_out, int64_t timeout_ms = 0) {
        auto start = std::chrono::steady_clock::now();
        std::chrono::milliseconds timeout(timeout_ms);

        while (true) {
            uint64_t id = expected_id_.load(std::memory_order_relaxed);
            size_t idx = id & (capacity_ - 1);

            // 如果 slot 已到达期望帧
            if (ring_buffer_[idx].filled->load(std::memory_order_acquire) &&
                ring_buffer_[idx].frame_id == id)
            {
                data_out = std::move(ring_buffer_[idx].data_);
                ring_buffer_[idx].frame_id = 0;
                ring_buffer_[idx].filled->store(false, std::memory_order_release);

                // 更新期望 id
                expected_id_.compare_exchange_strong(id, id + 1, std::memory_order_release);
                total_dequeued_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            if (timeout_ms > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed >= timeout) {
                    timeout_skip_count_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
            } else {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
    }

    // ===================== 查询接口 =====================

    // 近似队列大小
    size_t size() const {
        uint64_t enqueued = total_enqueued_.load(std::memory_order_relaxed);
        uint64_t dequeued = total_dequeued_.load(std::memory_order_relaxed);
        return static_cast<size_t>(enqueued > dequeued ? enqueued - dequeued : 0);
    }

    bool empty() const { return size() == 0; }
    uint64_t get_expected_id() const { return expected_id_.load(std::memory_order_relaxed); }

    // ===================== 统计接口 =====================

    struct Stats {
        uint64_t total_enqueued;
        uint64_t total_dequeued;
        uint64_t timeout_skip;
        uint64_t slot_conflict;
        uint64_t pending;
        double timeout_rate;
        double conflict_rate;
    };

    Stats get_stats() const {
        Stats stats;
        stats.total_enqueued = total_enqueued_.load(std::memory_order_relaxed);
        stats.total_dequeued = total_dequeued_.load(std::memory_order_relaxed);
        stats.timeout_skip = timeout_skip_count_.load(std::memory_order_relaxed);
        stats.slot_conflict = slot_conflict_count_.load(std::memory_order_relaxed);

        // 精确 pending 统计
        size_t pending_count = 0;
        for (const auto& slot : ring_buffer_)
            if (slot.filled->load(std::memory_order_relaxed)) ++pending_count;
        stats.pending = pending_count;

        stats.timeout_rate = stats.total_dequeued > 0 ?
            (double)stats.timeout_skip / stats.total_dequeued : 0.0;
        stats.conflict_rate = stats.total_enqueued > 0 ?
            (double)stats.slot_conflict / stats.total_enqueued : 0.0;
        return stats;
    }

    void print_stats() const {
        auto stats = get_stats();
        printf("\n===== OrderedQueue Statistics =====\n");
        printf("Enqueued:       %lu\n", stats.total_enqueued);
        printf("Dequeued:       %lu\n", stats.total_dequeued);
        printf("Pending:        %lu\n", stats.pending);
        printf("Timeout skip:   %lu (%.2f%%)\n",
               stats.timeout_skip, stats.timeout_rate*100);
        printf("Slot conflict:  %lu (%.2f%%)\n",
               stats.slot_conflict, stats.conflict_rate*100);
        printf("===================================\n\n");
    }

    void reset_stats() {
        total_enqueued_.store(0, std::memory_order_relaxed);
        total_dequeued_.store(0, std::memory_order_relaxed);
        timeout_skip_count_.store(0, std::memory_order_relaxed);
        slot_conflict_count_.store(0, std::memory_order_relaxed);
    }
};

#endif // ORDERED_QUEUE_H

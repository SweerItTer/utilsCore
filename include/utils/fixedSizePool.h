#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <algorithm>
#include <memory>

// 根据架构定义对齐大小和缓存参数
#ifdef __arm__
    #define ARCH_LINE_SIZE 64
    #define TLS_CACHE_SIZE 1024
    #define TLS_BATCH_SIZE 256
#else
    #define ARCH_LINE_SIZE 64 // 现代 x86 也是 64 字节缓存行
    #define TLS_CACHE_SIZE 2048
    #define TLS_BATCH_SIZE 512
#endif

// 编译器分支预测优化宏
#ifdef _MSC_VER
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#else
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

class FixedSizePool {
public:
    struct Node { Node* next; };

    // 关键优化: alignas(64) 确保每个线程的缓存独占缓存行, 防止伪共享
    struct alignas(ARCH_LINE_SIZE) ThreadCache {
        void* blocks[TLS_CACHE_SIZE];
        size_t count = 0;
        FixedSizePool* owner = nullptr;

        // 线程退出时自动回收内存到全局链表
        ~ThreadCache() {
            if (owner && count > 0) owner->flush_all(this);
        }
    };

    FixedSizePool(size_t blockSize, size_t blocksPerPage = 2048, size_t alignment = 64, size_t prealloc = 0)
        : alignment_(alignment), blocksPerPage_(blocksPerPage) {
        size_t minSize = sizeof(Node*);
        // 确保 blockSize 对齐
        blockSize_ = (std::max(blockSize, minSize) + alignment - 1) & ~(alignment - 1);
        if (prealloc > 0) expand(prealloc);
    }

    ~FixedSizePool() {
        std::lock_guard<std::mutex> lock(page_mutex_);
        for (auto& page : pages_) {
#ifdef _WIN32
            _aligned_free(page.first);
#else
            free(page.first);
#endif
        }
    }

    // 分配入口: 极其精简, 适合 inline 展开
    void* allocate() {
        ThreadCache* cache = get_fast_thread_cache();
        if (UNLIKELY(cache->count == 0)) {
            refill(cache);
            if (UNLIKELY(cache->count == 0)) return nullptr;
        }
        return cache->blocks[--cache->count];
    }

    // 释放入口
    void deallocate(void* p) {
        if (UNLIKELY(!p)) return;
        ThreadCache* cache = get_fast_thread_cache();
        if (UNLIKELY(cache->count >= TLS_CACHE_SIZE)) {
            flush(cache);
        }
        cache->blocks[cache->count++] = p;
    }

private:
    // 核心优化: 彻底移除 unordered_map, 使用真正的 TLS
    ThreadCache* get_fast_thread_cache() {
        static thread_local ThreadCache cache;
        if (UNLIKELY(cache.owner == nullptr)) {
            cache.owner = this;
        }
        return &cache;
    }

    void refill(ThreadCache* cache) {
        std::lock_guard<std::mutex> lock(central_mutex_);
        if (UNLIKELY(!freelist_head_)) expand_internal();

        // 尽量填满或拿走一个 Batch
        size_t take = std::min((size_t)TLS_BATCH_SIZE, (size_t)TLS_CACHE_SIZE - cache->count);
        for (size_t i = 0; i < take && freelist_head_; ++i) {
            Node* node = freelist_head_;
            freelist_head_ = node->next;
            cache->blocks[cache->count++] = node;
        }
    }

    void flush(ThreadCache* cache) {
        // 策略: 回冲一半缓存到全局, 保留一半在本地继续使用
        size_t to_flush = TLS_BATCH_SIZE; 
        Node* local_head = nullptr;
        Node* local_tail = nullptr;

        // 优化: 在锁外构造链表结构, 减少锁持有时间
        for (size_t i = 0; i < to_flush; ++i) {
            Node* node = (Node*)cache->blocks[--cache->count];
            node->next = local_head;
            local_head = node;
            if (!local_tail) local_tail = node;
        }

        std::lock_guard<std::mutex> lock(central_mutex_);
        local_tail->next = freelist_head_;
        freelist_head_ = local_head;
    }

    void flush_all(ThreadCache* cache) {
        if (cache->count == 0) return;
        Node* local_head = nullptr;
        Node* local_tail = nullptr;
        size_t temp_count = cache->count;
        for (size_t i = 0; i < temp_count; ++i) {
            Node* node = (Node*)cache->blocks[--cache->count];
            node->next = local_head;
            local_head = node;
            if (!local_tail) local_tail = node;
        }
        std::lock_guard<std::mutex> lock(central_mutex_);
        local_tail->next = freelist_head_;
        freelist_head_ = local_head;
    }

    void expand(size_t pages) {
        std::lock_guard<std::mutex> lock(expand_mutex_);
        for (size_t i = 0; i < pages; ++i) expand_internal();
    }

    void expand_internal() {
        size_t size = blockSize_ * blocksPerPage_;
        void* page = nullptr;
#ifdef _WIN32
        page = _aligned_malloc(size, alignment_);
#else
        if (posix_memalign(&page, alignment_, size) != 0) return;
#endif
        // 初始化新页面并链接成单向链表
        Node* local_head = nullptr;
        for (int i = (int)blocksPerPage_ - 1; i >= 0; --i) {
            Node* node = (Node*)((char*)page + i * blockSize_);
            node->next = local_head;
            local_head = node;
        }

        // 挂载到全局空闲链表头部
        Node* last_node = (Node*)((char*)page + (blocksPerPage_ - 1) * blockSize_);
        last_node->next = freelist_head_;
        freelist_head_ = (Node*)page;

        std::lock_guard<std::mutex> lock(page_mutex_);
        pages_.emplace_back(page, size);
    }

    Node* freelist_head_ = nullptr;
    std::mutex central_mutex_;
    std::mutex page_mutex_;
    std::mutex expand_mutex_;
    size_t blockSize_, blocksPerPage_, alignment_;
    std::vector<std::pair<void*, size_t>> pages_;
};
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iostream>
#include <cstring>

class FenceWatcher {
public:
    static FenceWatcher& instance() {
        static FenceWatcher watcher;
        return watcher;
    }

    // 异步等待 fence_fd, timeout_ms 可选, callback 超时或完成都会调用
    void watchFence(int fence_fd, std::function<void()> callback, int timeout_ms = 1000) {
        if (fence_fd < 0) {
            callback();
            return;
        }

        auto expire_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        {
            std::lock_guard<std::mutex> lock(map_mutex_);
            fd_callbacks_[fence_fd] = FenceData{std::move(callback), expire_time};
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fence_fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fence_fd, &ev) < 0) {
            if (errno == EEXIST) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fence_fd, &ev);
            } else {
                perror("epoll_ctl add fence_fd");
                triggerCallback(fence_fd);
            }
        }
    }

    void shutdown() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;

        uint64_t dummy = 1;
        write(event_fd_, &dummy, sizeof(dummy)); // 唤醒 epoll_wait
        if (loop_thread_.joinable()) loop_thread_.join();
    }

private:
    FenceWatcher() : running_(true) {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ < 0) {
            perror("epoll_create1");
        }

        event_fd_ = eventfd(0, EFD_NONBLOCK);
        if (event_fd_ < 0) {
            perror("eventfd");
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = event_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev);

        loop_thread_ = std::thread([this]() { this->eventLoop(); });
    }

    ~FenceWatcher() {
        shutdown();
        close(event_fd_);
        close(epoll_fd_);
    }

    struct FenceData {
        std::function<void()> callback;
        std::chrono::steady_clock::time_point expire_time;
    };

    void triggerCallback(int fd) {
        FenceData data;
        {
            std::lock_guard<std::mutex> lock(map_mutex_);
            auto it = fd_callbacks_.find(fd);
            if (it != fd_callbacks_.end()) {
                data = it->second;
                fd_callbacks_.erase(it);
            }
        }
        if (data.callback) {
            close(fd);
            data.callback();
        }
    }

    void eventLoop() {
        const int MAX_EVENTS = 16;
        struct epoll_event events[MAX_EVENTS];

        while (running_) {
            int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 50);
            auto now = std::chrono::steady_clock::now();

            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                if (fd == event_fd_) { // shutdown 唤醒
                    uint64_t dummy;
                    read(event_fd_, &dummy, sizeof(dummy));
                    continue;
                }
                triggerCallback(fd);
            }

            // 超时检查
            std::vector<int> expired;
            {
                std::lock_guard<std::mutex> lock(map_mutex_);
                for (auto& kv : fd_callbacks_) {
                    int fd = kv.first;
                    FenceData& data = kv.second;
                    if (now >= data.expire_time) expired.push_back(fd);
                }
            }

            for (size_t i = 0; i < expired.size(); ++i) {
                triggerCallback(expired[i]);
            }
        }

        // 退出前清理剩余 fence
        std::lock_guard<std::mutex> lock(map_mutex_);
        for (auto& kv : fd_callbacks_) {
            close(kv.first);
        }
        fd_callbacks_.clear();
    }

    int epoll_fd_ = -1;
    int event_fd_ = -1;
    std::unordered_map<int, FenceData> fd_callbacks_;
    std::mutex map_mutex_;
    std::thread loop_thread_;
    std::atomic<bool> running_;
};

#include "threadPauser.h"

// Linux 特定头文件
#include <sys/eventfd.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <system_error>

// 标准库头文件
#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

// 内部实现类
class ThreadPauser::Impl {
public:
    Impl() {
        // 创建 eventfd, 初始值为0(表示无信号)
        // EFD_CLOEXEC: 执行exec时关闭文件描述符
        // EFD_SEMAPHORE: 信号量模式, 每次read只减少1
        event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE);
        if (event_fd_ == -1) {
            throw std::system_error(errno, std::system_category(),
                                   "eventfd creation failed");
        }
    }
    
    // 自动关闭eventfd
    ~Impl() {
        if (event_fd_ != -1) {
            ::close(event_fd_);
            event_fd_ = -1;
        }
    }
    
    // 禁用拷贝
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    
    // 允许移动
    Impl(Impl&& other) noexcept
        : event_fd_(other.event_fd_)
        , paused_(other.paused_.load())
        , closed_(other.closed_.load()) {
        other.event_fd_ = -1;
        other.closed_.store(true, std::memory_order_release);
    }
    
    Impl& operator=(Impl&& other) noexcept {
        if (this != &other) {
            close();
            event_fd_ = other.event_fd_;
            paused_.store(other.paused_.load(), std::memory_order_release);
            closed_.store(other.closed_.load(), std::memory_order_release);
            other.event_fd_ = -1;
            other.closed_.store(true, std::memory_order_release);
        }
        return *this;
    }
    
    // 暂停时等待
    void wait_if_paused() {
        if (closed_.load(std::memory_order_acquire)) {
            return;
        }
        
        // 快速检查: 如果未暂停, 直接返回
        if (!paused_.load(std::memory_order_acquire)) {
            return;
        }
        
        // 如果已暂停, 阻塞等待恢复信号
        uint64_t value;
        ssize_t ret;
        
        do {
            // read 系统调用, 在可读前内核将会切换线程状态为睡眠态
            ret = read(event_fd_, &value, sizeof(value));
        } while (ret == -1 && errno == EINTR);  // 被信号(ctrl+c)中断, 继续等待
        
        if (ret == -1 && !closed_.load(std::memory_order_acquire)) {
            throw std::system_error(errno, std::system_category(),
                                   "eventfd read failed in wait_if_paused");
        }
        
        // 避免虚假唤醒或未退出函数又被置为暂停
        if (paused_.load(std::memory_order_acquire)) {
            wait_if_paused();   // 继续等待
        }
    }
    
    bool wait_if_paused_for(int timeout_ms) {
        if (closed_.load(std::memory_order_acquire)) {
            return true;
        }
        
        // 快速检查
        if (!paused_.load(std::memory_order_acquire)) {
            return true;
        }
        
        // 使用 poll 进行超时等待
        pollfd pfd{event_fd_, POLLIN, 0};
        int ret = poll(&pfd, 1, timeout_ms);
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            // 有数据可读, 读取并检查状态
            uint64_t value;
            if (read(event_fd_, &value, sizeof(value)) == -1) {
                if (!closed_.load(std::memory_order_acquire)) {
                    throw std::system_error(errno, std::system_category(),
                                           "eventfd read failed in wait_if_paused_for");
                }
                return false;
            }
            
            // 检查是否真的恢复了
            return !paused_.load(std::memory_order_acquire);
        }
        
        // 超时或出错
        return false;
    }
    
    void pause() {
        if (closed_.load(std::memory_order_acquire)) {
            return;
        }
        
        paused_.store(true, std::memory_order_release);
    }
    
    void resume() {
        if (closed_.load(std::memory_order_acquire)) {
            return;
        }
        
        bool was_paused = paused_.exchange(false, std::memory_order_acq_rel);
        
        // 如果之前是暂停状态, 发送恢复信号
        if (was_paused) {
            uint64_t value = 1;
            if (write(event_fd_, &value, sizeof(value)) == -1) {
                throw std::system_error(errno, std::system_category(),
                                       "eventfd write failed in resume");
            }
        }
    }

    // 状态切换
    void toggle() {
        if (closed_.load(std::memory_order_acquire)) {
            return;
        }
        
        if (paused_.load(std::memory_order_acquire)) {
            resume();
        } else {
            pause();
        }
    }
    
    bool is_paused() const {
        return paused_.load(std::memory_order_acquire);
    }
    
    bool is_closed() const {
        return closed_.load(std::memory_order_acquire);
    }
    
    void close() {
        if (closed_.exchange(true, std::memory_order_release)) {
            return;  // 已经关闭过了
        }
        
        // 确保所有等待的线程都能退出
        if (paused_.exchange(false, std::memory_order_acq_rel)) {
            uint64_t value = 1;
            write(event_fd_, &value, sizeof(value));
        }
        
        if (event_fd_ != -1) {
            ::close(event_fd_);
            event_fd_ = -1;
        }
    }

private:
    int event_fd_{-1};
    std::atomic<bool> paused_{false};
    std::atomic<bool> closed_{false};
};

// ThreadPauser 公共接口实现
ThreadPauser::ThreadPauser() : impl_(std::make_unique<Impl>()) {}

ThreadPauser::~ThreadPauser() {
    // 自动调用 close()
    if (impl_) {
        impl_->close();
    }
}

ThreadPauser::ThreadPauser(ThreadPauser&& other) noexcept
    : impl_(std::move(other.impl_)) {}

ThreadPauser& ThreadPauser::operator=(ThreadPauser&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void ThreadPauser::wait_if_paused() {
    ensure_not_closed();
    impl_->wait_if_paused();
}

bool ThreadPauser::wait_if_paused_for(int timeout_ms) {
    ensure_not_closed();
    return impl_->wait_if_paused_for(timeout_ms);
}

void ThreadPauser::pause() {
    ensure_not_closed();
    impl_->pause();
}

void ThreadPauser::resume() {
    ensure_not_closed();
    impl_->resume();
}

void ThreadPauser::toggle() {
    ensure_not_closed();
    impl_->toggle();
}

bool ThreadPauser::is_paused() const {
    ensure_not_closed();
    return impl_->is_paused();
}

void ThreadPauser::close() {
    if (impl_) {
        impl_->close();
    }
}

bool ThreadPauser::is_closed() const {
    return impl_ ? impl_->is_closed() : true;
}

void ThreadPauser::ensure_not_closed() const {
    if (is_closed()) {
        throw std::runtime_error("ThreadPauser is closed");
    }
}
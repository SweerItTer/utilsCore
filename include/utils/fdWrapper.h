/*
 * @FilePath: /include/utils/fdWrapper.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-08 15:40:41
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#ifndef FD_WRAPPER_H
#define FD_WRAPPER_H

#include <fcntl.h>    // O_RDWR, O_CLOEXEC, open
#include <unistd.h>   // close

class FdWrapper {
public:
    explicit FdWrapper(int fd = -1) : fd_(fd) {}

    // 禁止拷贝
    FdWrapper(const FdWrapper&) = delete;
    FdWrapper& operator=(const FdWrapper&) = delete;

    // 实现移动构造
    FdWrapper(FdWrapper&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;  // 转移所有权, 避免原对象析构时关闭fd
    }

    // 实现移动赋值
    FdWrapper& operator=(FdWrapper&& other) noexcept {
        if (this != &other) {
            closeFd();       // 先关闭当前fd(如果有效)
            fd_ = other.fd_; // 接管fd
            other.fd_ = -1;  // 清空原对象fd, 避免重复关闭
        }
        return *this;
    }

    ~FdWrapper() {
        closeFd();
    }

    int get() const { return fd_; }

private:
    int fd_;

    void closeFd() {
        if (fd_ >= 0) {
            fprintf(stdout, "[FdWrapper] close fd: %d\n", fd_);
            close(fd_);
            fd_ = -1;
        }
    }
};
            

#endif // !FD_WRAPPER_H
    
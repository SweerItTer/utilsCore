/*
 * @FilePath: /EdgeVision/include/utils/fdWrapper.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-08 15:40:41
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#ifndef FD_WRAPPER_H
#define FD_WRAPPER_H

class FdWrapper {
public:
    explicit FdWrapper(int fd = -1) : fd_(fd) {}
    ~FdWrapper() { if (fd_ >= 0) close(fd_); }
    int get() const { return fd_; }
private:
    int fd_;
};

#endif // !FD_WRAPPER_H
    
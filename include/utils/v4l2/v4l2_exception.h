/*
 * @FilePath: /EdgeVision/include/utils/v4l2/v4l2_exception.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-04 20:18:22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef V4L2_EXCEPTION_H
#define V4L2_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <cstring>

/* 所有构造完成的类在 throw 的时候会调用对应的析构函数
 * 但是如果没有 catch 程序将会直接崩溃退出
 */
class V4L2Exception : public std::runtime_error {
public:
    V4L2Exception(const std::string& msg, int err = 0)
        : std::runtime_error(msg + (err ? (": " + std::string(strerror(err))) : "")){}
};

#endif // !V4L2_EXCEPTION_H
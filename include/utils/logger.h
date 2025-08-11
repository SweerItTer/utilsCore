/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-09 16:56:24
 * @FilePath: /EdgeVision/include/utils/logger.h
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <mutex>
#include <string.h>
#include <cstdio>
#include <chrono>
#include <string>
#include <ctime>
#include <sys/time.h>
#include <sstream>
#include <iomanip>
#include <cstdarg>
#include <stdexcept>
#include <iostream>

class FileStream {
public:
    // 默认构造函数 - 初始化为空指针
    FileStream() noexcept : fp_(nullptr) {}
    
    // 从 FILE* 构造 - 接管所有权
    explicit FileStream(FILE* fp) noexcept : fp_(fp) {}
    
    // 移动构造函数
    FileStream(FileStream&& other) noexcept : fp_(other.fp_) {
        other.fp_ = nullptr; // 重要：转移后置空原指针
    }
    
    // 移动赋值运算符
    FileStream& operator=(FileStream&& other) noexcept {
        if (this != &other) {
            reset(); // 释放当前资源
            fp_ = other.fp_;
            other.fp_ = nullptr;
        }
        return *this;
    }
    
    // 禁止拷贝
    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;
    
    ~FileStream() {
        reset();
    }
    
    // 获取原始指针
    FILE* get() const noexcept {
        return fp_;
    }
    
    // 释放所有权并返回指针
    FILE* release() noexcept {
        FILE* fp = fp_;
        fp_ = nullptr;
        return fp;
    }
    
    // 重置资源（安全关闭文件）
    void reset() noexcept {
        if (fp_) {
            fclose(fp_);
            fp_ = nullptr;
        }
    }
    
    // 重置为新的文件指针
    void reset(FILE* fp) noexcept {
        reset();
        fp_ = fp;
    }
    
    // 检查是否有效
    explicit operator bool() const noexcept {
        return fp_ != nullptr;
    }

private:
    FILE* fp_;
};

/**
 * @class Logger
 * @brief 线程安全的日志记录器，提供静态方法进行日志记录
 * 
 * 本日志记录器实现以下功能：
 * 1. 单例模式(静态方法调用)
 * 2. 线程安全的日志写入
 * 3. 毫秒级时间戳
 * 4. 格式化输出支持
 * 5. 自动管理日志文件描述符
 * 
 * 使用示例：
 * @code
 * Logger::initLogger();  // 初始化日志系统
 * Logger::log("Application started");  // 简单日志
 * Logger::log("Sensor value: %.2f, status: %d", 23.5f, 1);  // 格式化日志
 * @endcode
 */
class Logger
{
private:
    static FileStream logfileFp;      ///< 日志文件描述符包装对象，RAII管理文件资源
    static std::mutex logMutex;      ///< 互斥锁，确保线程安全的日志写入
    Logger() = default;             ///< 私有构造函数，防止实例化
    ~Logger() = default;            ///< 私有析构函数

public:
    static bool LogFlag;

    /**
     * @brief 初始化日志系统
     * 
     * 创建按时间命名的日志文件，格式为 YYYY-MM-DD_HH-MM-SS.log
     * 初始化必须在其他日志方法前调用(建议在main函数中使用)
     * 
     * 示例：
     * @code
     * Logger::initLogger();
     * @endcode
     */
    static void initLogger();
    
    /**
     * @brief 记录格式化日志
     * 
     * 线程安全的日志记录方法，支持 printf 风格格式化
     * 自动添加时间戳格式：[YYYY-MM-DD HH:MM:SS.mmm]
     * 
     * @param format 格式化字符串，遵循标准 printf 格式规范
     * @param ... 可变参数，匹配格式化字符串
     * 
     * 示例：
     * @code
     * // 记录简单消息
     * Logger::log(stdout, "Service started");
     * 
     * // 带参数的格式化日志
     * int error_code = 404;
     * Logger::log(stderr, "Request failed with error: %d", error_code);
     * 
     * // 多参数日志
     * float temp = 23.5f;
     * Logger::log(stdout, "Temperature: %.1f°C, Humidity: %d%%", temp, 45);
     * @endcode
     * 
     * @note 如果日志系统未初始化，消息将被静默丢弃
     */
    static void log(FILE *stream, const char* format, ...);
};


namespace mk{
    // 生成当前时间戳字符串 + 微秒部分
    inline std::string makeTimestamp(uint64_t &out_us_epoch) {
        // 获取单调时钟时间点
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
    
        // 计算微秒时间戳(单调时钟基准 CLOCK_MONOTONIC)
        /*  tv_sec  -> 秒
            tv_nsec -> 纳秒
            tv_usec -> 微秒
            out_us_epoch -> 微秒*/
        out_us_epoch = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + ts.tv_nsec / 1000ULL;
        
        
        /* == 获取系统实际时间用于log记录 == */
        struct timeval tv;
        gettimeofday(&tv, nullptr);
    
        std::time_t sec = tv.tv_sec;
        int ms = tv.tv_usec / 1000;
    
        std::tm tm_buf;
        localtime_r(&sec, &tm_buf);
    
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms;
    
        return oss.str();
    }

    /**
     * @brief 输出时间差到日志
     * 
     * @param t1 上一个时间,在调用函数前的某个时间点
     * @param msg 描述时间差的字符串
     * 
     * 示例：
     * @code
     * mk::timeDiffMs(t, "[Deququ->rga]");
     * @endcode
     */
    inline auto timeDiffMs(uint64_t t1, const char* msg) -> uint64_t{
        if (false == Logger::LogFlag) return 0;
        
        uint64_t t2;
        mk::makeTimestamp(t2);
        if (t2 < t1) return t2;
        double delta_ms = static_cast<double>(t2-t1) / 1000.0;
        Logger::log(stdout, "%s = %.3f ms", msg, delta_ms);
        return t2;
    }
}
#endif // LOGGER_H
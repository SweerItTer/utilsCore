/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-09 18:57:46
 * @FilePath: /src/utils/logger.cpp
 */
#include "logger.h"

FileStream Logger::logfileFp;
std::mutex Logger::logMutex;
bool Logger::LogFlag;

void Logger::initLogger() {
    uint64_t dummy;
    auto ts = mk::makeTimestamp(dummy);

    // 用当前时间戳生成文件名
    std::string filename = ts.substr(0, 19); // 取 YYYY-MM-DD HH:MM:SS
    for (auto &ch : filename) {
        if (ch == ' ' || ch == ':') ch = '-';
    }
    filename += ".log";

    FILE* fp = std::fopen(filename.c_str(), "a");
    if (nullptr == fp) {
        std::fprintf(stderr, "[ERROR] Log file (%s) open failed: %s\n", 
                     filename.c_str(), strerror(errno));
        return;
    }
    logfileFp = FileStream(fp);

    std::printf("Logging to: %s\n", filename.c_str());
}

void Logger::log(FILE *stream, const char* format, ...) {
    if (false == LogFlag) return;
    std::lock_guard<std::mutex> lock(logMutex);

    uint64_t ts_us;
    std::string timestamp = mk::makeTimestamp(ts_us);

    // 处理可变参数
    va_list args;
    va_start(args, format);

    // 输出到日志文件
    if (logfileFp) {
        std::fprintf(logfileFp.get(), "[%s] ", timestamp.c_str());
        vfprintf(logfileFp.get(), format, args);
        std::fprintf(logfileFp.get(), "\n");
        std::fflush(logfileFp.get());
    }

    // 输出到指定流
    va_list args_copy;
    va_copy(args_copy, args);
    std::fprintf(stream, "[%s] ", timestamp.c_str());
    vfprintf(stream, format, args_copy);
    std::fprintf(stream, "\n");
    std::fflush(stream);
    va_end(args_copy);

    va_end(args);
}

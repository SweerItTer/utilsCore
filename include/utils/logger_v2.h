#ifndef LOGGER_V2_H
#define LOGGER_V2_H

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "concurrentqueue.h"

namespace utils {

enum class LogLevel : unsigned char {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    WARNING = 3,  // backward compatibility alias
    ERROR = 4,
    FATAL = 5,
    OFF = 6,
};

inline const char* logLevelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::OFF: return "OFF";
        default: return "UNKNOWN";
    }
}

inline LogLevel stringToLogLevel(const std::string& level) noexcept {
    if (level == "TRACE") return LogLevel::TRACE;
    if (level == "DEBUG") return LogLevel::DEBUG;
    if (level == "INFO") return LogLevel::INFO;
    if (level == "WARN" || level == "WARNING") return LogLevel::WARN;
    if (level == "ERROR") return LogLevel::ERROR;
    if (level == "FATAL") return LogLevel::FATAL;
    if (level == "OFF") return LogLevel::OFF;
    return LogLevel::INFO;
}

using LogFields = std::unordered_map<std::string, std::string>;

struct LogMessage {
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread_id;
    std::string file;
    int line;
    std::string function;
    std::string message;
    LogFields fields;

    LogMessage()
        : level(LogLevel::INFO)
        , timestamp(std::chrono::system_clock::now())
        , thread_id(std::this_thread::get_id())
        , line(0) {}

    LogMessage(LogLevel lvl,
               const char* f,
               int ln,
               const char* func,
               const std::string& msg,
               LogFields flds = LogFields())
        : level(lvl)
        , timestamp(std::chrono::system_clock::now())
        , thread_id(std::this_thread::get_id())
        , file(f ? f : "")
        , line(ln)
        , function(func ? func : "")
        , message(msg)
        , fields(std::move(flds)) {}
};

class LogSink {
public:
    virtual ~LogSink() {}
    virtual void write(const LogMessage& msg) = 0;
    virtual void flush() = 0;
    virtual void setPattern(const std::string& pattern) = 0;
    virtual bool shouldLog(LogLevel level) const = 0;
};

class ConsoleSink : public LogSink {
public:
    explicit ConsoleSink(FILE* stream = stdout, LogLevel min_level = LogLevel::TRACE);

    void write(const LogMessage& msg) override;
    void flush() override;
    void setPattern(const std::string& pattern) override;
    bool shouldLog(LogLevel level) const override;

    void setUseColors(bool use_colors) { use_colors_ = use_colors; }
    void setUseStderr(bool use_stderr) { stream_ = use_stderr ? stderr : stdout; }

private:
    FILE* stream_;
    LogLevel min_level_;
    bool use_colors_;
    std::string pattern_;
    std::mutex mutex_;

    std::string formatMessage(const LogMessage& msg) const;
    const char* getColorCode(LogLevel level) const;
};

class FileSink : public LogSink {
public:
    explicit FileSink(const std::string& filename, LogLevel min_level = LogLevel::INFO);
    ~FileSink() override;

    void write(const LogMessage& msg) override;
    void flush() override;
    void setPattern(const std::string& pattern) override;
    bool shouldLog(LogLevel level) const override;

    size_t getFileSize() const;
    bool reopen(const std::string& new_filename = "");

private:
    std::string filename_;
    LogLevel min_level_;
    std::string pattern_;
    mutable std::mutex mutex_;
    FILE* file_;

    std::string formatMessage(const LogMessage& msg) const;
    bool openFile();
    void closeFile();
};

class AsyncLogQueue {
public:
    explicit AsyncLogQueue(size_t capacity = 8192);
    ~AsyncLogQueue();

    void start();
    void stop();
    bool push(LogMessage&& msg);
    size_t size() const;

    void addSink(std::shared_ptr<LogSink> sink);
    void clearSinks();
    void setFlushInterval(int ms);
    void setPattern(const std::string& pattern);
    void flushSinks();

private:
    void workerThread();

    moodycamel::ConcurrentQueue<LogMessage> queue_;
    std::vector<std::shared_ptr<LogSink> > sinks_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::atomic<int> flush_interval_ms_;
    mutable std::mutex sinks_mutex_;
};

// LoggerConfig is defined in logger_config.h (to avoid circular deps).
struct LoggerConfig;

class LoggerV2 {
public:
    LoggerV2() = delete;
    ~LoggerV2() = delete;

    static void init();
    static void init(const LoggerConfig& config);
    static void shutdown();

    static void setLevel(LogLevel level);
    static LogLevel getLevel();
    static bool shouldLog(LogLevel level);

    template <typename... Args>
    static void log(LogLevel level,
                    const char* file,
                    int line,
                    const char* function,
                    const char* format,
                    Args... args) {
        if (!shouldLog(level)) {
            return;
        }
        ensureInitialized();

        const std::string message = formatMessage(format, args...);
        LogMessage msg(level, file, line, function, message);
        dispatch(std::move(msg));
    }

    template <typename... Args>
    static void logWithFields(LogLevel level,
                              const char* file,
                              int line,
                              const char* function,
                              const LogFields& fields,
                              const char* format,
                              Args... args) {
        if (!shouldLog(level)) {
            return;
        }
        ensureInitialized();

        const std::string message = formatMessage(format, args...);
        LogMessage msg(level, file, line, function, message, fields);
        dispatch(std::move(msg));
    }

    static void addSink(std::shared_ptr<LogSink> sink);
    static void flush();
    static size_t queueSize();
    static void setPattern(const std::string& pattern);

private:
    static std::unique_ptr<AsyncLogQueue> queue_;
    static std::vector<std::shared_ptr<LogSink> > sync_sinks_;
    static std::atomic<LogLevel> global_level_;
    static std::atomic<bool> initialized_;
    static std::mutex init_mutex_;
    static bool async_mode_;

    static void ensureInitialized();
    static void dispatch(LogMessage&& msg);

    static std::string formatMessage(const char* format) {
        return format ? std::string(format) : std::string();
    }

    template <typename... Args>
    static std::string formatMessage(const char* format, Args... args) {
        if (format == nullptr) {
            return std::string();
        }

        int needed = std::snprintf(nullptr, 0, format, args...);
        if (needed <= 0) {
            return std::string(format);
        }

        std::vector<char> buf(static_cast<size_t>(needed) + 1, '\0');
        std::snprintf(buf.data(), buf.size(), format, args...);
        return std::string(buf.data(), static_cast<size_t>(needed));
    }
};

inline bool LoggerV2::shouldLog(LogLevel level) {
    return level >= global_level_.load(std::memory_order_relaxed)
        && global_level_.load(std::memory_order_relaxed) != LogLevel::OFF;
}

inline LogLevel LoggerV2::getLevel() {
    return global_level_.load(std::memory_order_relaxed);
}

inline void LoggerV2::setLevel(LogLevel level) {
    global_level_.store(level, std::memory_order_relaxed);
}

inline size_t LoggerV2::queueSize() {
    if (!queue_) {
        return 0;
    }
    return queue_->size();
}

#define LOG_TRACE(...)    \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::TRACE)) \
            utils::LoggerV2::log(utils::LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(...)    \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::DEBUG)) \
            utils::LoggerV2::log(utils::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_INFO(...)     \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::INFO)) \
            utils::LoggerV2::log(utils::LogLevel::INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_WARN(...)     \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::WARN)) \
            utils::LoggerV2::log(utils::LogLevel::WARN, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_ERROR(...)    \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::ERROR)) \
            utils::LoggerV2::log(utils::LogLevel::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_FATAL(...)    \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::FATAL)) \
            utils::LoggerV2::log(utils::LogLevel::FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_TRACE_IF(cond, ...) \
    do { \
        if ((cond) && utils::LoggerV2::shouldLog(utils::LogLevel::TRACE)) \
            utils::LoggerV2::log(utils::LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG_IF(cond, ...) \
    do { \
        if ((cond) && utils::LoggerV2::shouldLog(utils::LogLevel::DEBUG)) \
            utils::LoggerV2::log(utils::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_INFO_IF(cond, ...) \
    do { \
        if ((cond) && utils::LoggerV2::shouldLog(utils::LogLevel::INFO)) \
            utils::LoggerV2::log(utils::LogLevel::INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

#define LOG_TRACE_FIELDS(fields, ...) \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::TRACE)) \
            utils::LoggerV2::logWithFields(utils::LogLevel::TRACE, __FILE__, __LINE__, __func__, fields, __VA_ARGS__); \
    } while (0)

#define LOG_DEBUG_FIELDS(fields, ...) \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::DEBUG)) \
            utils::LoggerV2::logWithFields(utils::LogLevel::DEBUG, __FILE__, __LINE__, __func__, fields, __VA_ARGS__); \
    } while (0)

#define LOG_INFO_FIELDS(fields, ...) \
    do { \
        if (utils::LoggerV2::shouldLog(utils::LogLevel::INFO)) \
            utils::LoggerV2::logWithFields(utils::LogLevel::INFO, __FILE__, __LINE__, __func__, fields, __VA_ARGS__); \
    } while (0)

#define LOG_TRACE_ONCE(...) \
    do { \
        static bool logged = false; \
        if (!logged && utils::LoggerV2::shouldLog(utils::LogLevel::TRACE)) { \
            logged = true; \
            utils::LoggerV2::log(utils::LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__); \
        } \
    } while (0)

#define LOG_DEBUG_ONCE(...) \
    do { \
        static bool logged = false; \
        if (!logged && utils::LoggerV2::shouldLog(utils::LogLevel::DEBUG)) { \
            logged = true; \
            utils::LoggerV2::log(utils::LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__); \
        } \
    } while (0)

#define LOG_INFO_ONCE(...) \
    do { \
        static bool logged = false; \
        if (!logged && utils::LoggerV2::shouldLog(utils::LogLevel::INFO)) { \
            logged = true; \
            utils::LoggerV2::log(utils::LogLevel::INFO, __FILE__, __LINE__, __func__, __VA_ARGS__); \
        } \
    } while (0)

#define LOG_COMPAT(level, ...) \
    do { \
        utils::LogLevel lvl = utils::stringToLogLevel(level); \
        if (utils::LoggerV2::shouldLog(lvl)) \
            utils::LoggerV2::log(lvl, __FILE__, __LINE__, __func__, __VA_ARGS__); \
    } while (0)

}  // namespace utils

#endif  // LOGGER_V2_H

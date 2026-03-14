#include "logger_config.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace utils {

namespace {

std::string shortenSourcePath(const std::string& filePath) {
    const size_t slashPos = filePath.find_last_of("/\\");
    if (slashPos == std::string::npos) {
        return filePath;
    }
    return filePath.substr(slashPos + 1);
}

std::string formatWithPattern(const std::string& pattern, const LogMessage& msg, bool short_file) {
    std::string result = pattern;

    const std::time_t t = std::chrono::system_clock::to_time_t(msg.timestamp);
    std::tm tm_buf;
    localtime_r(&t, &tm_buf);

    char time_buf[64] = {0};
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    const long long ms = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.timestamp.time_since_epoch())
            .count() % 1000);

    const std::string time_with_ms = std::string(time_buf) + "." + std::to_string(ms);

    std::string file = msg.file;
    if (short_file || !file.empty()) {
        file = shortenSourcePath(file);
    }

    std::stringstream tid;
    tid << msg.thread_id;

    size_t pos = std::string::npos;
    while ((pos = result.find("%Y-%m-%d %H:%M:%S.%e")) != std::string::npos) {
        result.replace(pos, 20, time_with_ms);
    }
    while ((pos = result.find("%l")) != std::string::npos) {
        result.replace(pos, 2, logLevelToString(msg.level));
    }
    while ((pos = result.find("%t")) != std::string::npos) {
        result.replace(pos, 2, tid.str());
    }
    while ((pos = result.find("%s")) != std::string::npos) {
        result.replace(pos, 2, file);
    }
    while ((pos = result.find("%#")) != std::string::npos) {
        result.replace(pos, 2, std::to_string(msg.line));
    }
    while ((pos = result.find("%f")) != std::string::npos) {
        result.replace(pos, 2, msg.function);
    }
    while ((pos = result.find("%v")) != std::string::npos) {
        result.replace(pos, 2, msg.message);
    }

    if (!msg.fields.empty()) {
        result += " {";
        bool first = true;
        for (LogFields::const_iterator it = msg.fields.begin(); it != msg.fields.end(); ++it) {
            if (!first) {
                result += ", ";
            }
            result += it->first;
            result += "=";
            result += it->second;
            first = false;
        }
        result += "}";
    }

    return result;
}

}  // namespace

ConsoleSink::ConsoleSink(FILE* stream, LogLevel min_level)
    : stream_(stream)
    , min_level_(min_level)
    , use_colors_(true)
    , pattern_("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v") {
    const char* term = std::getenv("TERM");
    if (!term) {
        use_colors_ = false;
    }
}

void ConsoleSink::write(const LogMessage& msg) {
    if (!shouldLog(msg.level)) {
        return;
    }

    const std::string text = formatMessage(msg);
    std::lock_guard<std::mutex> lock(mutex_);
    if (use_colors_) {
        std::fprintf(stream_, "%s%s\033[0m\n", getColorCode(msg.level), text.c_str());
    } else {
        std::fprintf(stream_, "%s\n", text.c_str());
    }
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fflush(stream_);
}

void ConsoleSink::setPattern(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    pattern_ = pattern;
}

bool ConsoleSink::shouldLog(LogLevel level) const {
    return level >= min_level_ && min_level_ != LogLevel::OFF;
}

std::string ConsoleSink::formatMessage(const LogMessage& msg) const {
    return formatWithPattern(pattern_, msg, false);
}

const char* ConsoleSink::getColorCode(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "\033[90m";
        case LogLevel::DEBUG: return "\033[36m";
        case LogLevel::INFO: return "\033[32m";
        case LogLevel::WARN: return "\033[33m";
        case LogLevel::ERROR: return "\033[31m";
        case LogLevel::FATAL: return "\033[35m";
        default: return "\033[0m";
    }
}

FileSink::FileSink(const std::string& filename, LogLevel min_level)
    : filename_(filename)
    , min_level_(min_level)
    , pattern_("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v")
    , file_(nullptr) {
    openFile();
}

FileSink::~FileSink() {
    closeFile();
}

void FileSink::write(const LogMessage& msg) {
    if (!shouldLog(msg.level)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) {
        return;
    }
    const std::string text = formatMessage(msg);
    std::fprintf(file_, "%s\n", text.c_str());
}

void FileSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fflush(file_);
    }
}

void FileSink::setPattern(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(mutex_);
    pattern_ = pattern;
}

bool FileSink::shouldLog(LogLevel level) const {
    return level >= min_level_ && min_level_ != LogLevel::OFF;
}

size_t FileSink::getFileSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) {
        return 0;
    }
    const long pos = std::ftell(file_);
    return pos < 0 ? 0 : static_cast<size_t>(pos);
}

bool FileSink::reopen(const std::string& new_filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!new_filename.empty()) {
        filename_ = new_filename;
    }
    closeFile();
    return openFile();
}

std::string FileSink::formatMessage(const LogMessage& msg) const {
    return formatWithPattern(pattern_, msg, true);
}

bool FileSink::openFile() {
    const size_t slash_pos = filename_.find_last_of("/\\");
    if (slash_pos != std::string::npos) {
        const std::string dir = filename_.substr(0, slash_pos);
        if (!dir.empty()) {
            mkdir(dir.c_str(), 0755);
        }
    }

    file_ = std::fopen(filename_.c_str(), "a");
    if (!file_) {
        std::fprintf(stderr, "[logger_v2] open file failed: %s (%s)\n", filename_.c_str(), std::strerror(errno));
        return false;
    }
    std::setvbuf(file_, NULL, _IOFBF, 8192);
    return true;
}

void FileSink::closeFile() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

AsyncLogQueue::AsyncLogQueue(size_t capacity, LogOverflowPolicy overflow_policy)
    : queue_(capacity)
    , running_(false)
    , flush_interval_ms_(1000)
    , queued_count_(0)
    , capacity_hint_(capacity == 0 ? 1 : capacity)
    , overflow_policy_(overflow_policy)
    , pushed_count_(0)
    , dropped_count_(0)
    , dropped_trace_count_(0)
    , dropped_debug_count_(0)
    , dropped_info_count_(0)
    , dropped_warn_count_(0) {}

AsyncLogQueue::~AsyncLogQueue() {
    stop();
}

void AsyncLogQueue::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&AsyncLogQueue::workerThread, this);
}

void AsyncLogQueue::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    LogMessage msg;
    while (queue_.try_dequeue(msg)) {
        queued_count_.fetch_sub(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(sinks_mutex_);
        for (size_t i = 0; i < sinks_.size(); ++i) {
            if (sinks_[i] && sinks_[i]->shouldLog(msg.level)) {
                sinks_[i]->write(msg);
            }
        }
    }

    flushSinks();
}

bool AsyncLogQueue::push(LogMessage&& msg) {
    pushed_count_.fetch_add(1, std::memory_order_relaxed);
    if (overflow_policy_ == LogOverflowPolicy::DropIfBelowError && shouldDrop(msg)) {
        recordDrop(msg.level);
        return false;
    }

    if (overflow_policy_ == LogOverflowPolicy::Block) {
        while (running_.load(std::memory_order_relaxed) &&
               queued_count_.load(std::memory_order_relaxed) >= capacity_hint_) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        return tryPush(std::move(msg));
    }

    if (queued_count_.load(std::memory_order_relaxed) >= capacity_hint_) {
        recordDrop(msg.level);
        return false;
    }
    return tryPush(std::move(msg));
}

size_t AsyncLogQueue::size() const {
    return queued_count_.load(std::memory_order_relaxed);
}

size_t AsyncLogQueue::capacity() const {
    return capacity_hint_;
}

LogQueueStats AsyncLogQueue::stats() const {
    LogQueueStats currentStats;
    currentStats.queued = queued_count_.load(std::memory_order_relaxed);
    currentStats.pushed = pushed_count_.load(std::memory_order_relaxed);
    currentStats.dropped = dropped_count_.load(std::memory_order_relaxed);
    currentStats.dropped_trace = dropped_trace_count_.load(std::memory_order_relaxed);
    currentStats.dropped_debug = dropped_debug_count_.load(std::memory_order_relaxed);
    currentStats.dropped_info = dropped_info_count_.load(std::memory_order_relaxed);
    currentStats.dropped_warn = dropped_warn_count_.load(std::memory_order_relaxed);
    return currentStats;
}

bool AsyncLogQueue::hasSinks() const {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    return !sinks_.empty();
}

void AsyncLogQueue::addSink(std::shared_ptr<LogSink> sink) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back(sink);
}

void AsyncLogQueue::clearSinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();
}

void AsyncLogQueue::setFlushInterval(int ms) {
    if (ms <= 0) {
        ms = 1000;
    }
    flush_interval_ms_.store(ms, std::memory_order_relaxed);
}

void AsyncLogQueue::setPattern(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (size_t i = 0; i < sinks_.size(); ++i) {
        if (sinks_[i]) {
            sinks_[i]->setPattern(pattern);
        }
    }
}

void AsyncLogQueue::flushSinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (size_t i = 0; i < sinks_.size(); ++i) {
        if (sinks_[i]) {
            sinks_[i]->flush();
        }
    }
}

void AsyncLogQueue::workerThread() {
    LogMessage msg;
    std::chrono::steady_clock::time_point last_flush = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        bool got_one = queue_.try_dequeue(msg);
        if (got_one) {
            queued_count_.fetch_sub(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(sinks_mutex_);
            for (size_t i = 0; i < sinks_.size(); ++i) {
                if (sinks_[i] && sinks_[i]->shouldLog(msg.level)) {
                    sinks_[i]->write(msg);
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush).count();
        if (elapsed >= flush_interval_ms_.load(std::memory_order_relaxed)) {
            flushSinks();
            last_flush = now;
        }
    }
}

bool AsyncLogQueue::shouldDrop(const LogMessage& msg) const {
    return msg.level < LogLevel::ERROR &&
           queued_count_.load(std::memory_order_relaxed) >= capacity_hint_;
}

bool AsyncLogQueue::tryPush(LogMessage&& msg) {
    const LogLevel level = msg.level;
    if (!queue_.try_enqueue(std::move(msg))) {
        recordDrop(level);
        return false;
    }
    queued_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void AsyncLogQueue::recordDrop(LogLevel level) {
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    switch (level) {
        case LogLevel::TRACE:
            dropped_trace_count_.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogLevel::DEBUG:
            dropped_debug_count_.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogLevel::INFO:
            dropped_info_count_.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogLevel::WARN:
            dropped_warn_count_.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

std::unique_ptr<AsyncLogQueue> LoggerV2::queue_;
std::vector<std::shared_ptr<LogSink> > LoggerV2::sync_sinks_;
std::atomic<LogLevel> LoggerV2::global_level_(LogLevel::INFO);
std::atomic<bool> LoggerV2::initialized_(false);
std::mutex LoggerV2::init_mutex_;
bool LoggerV2::async_mode_ = true;

void LoggerV2::init() {
    LoggerV2::init(LoggerConfig::defaultConfig());
}

void LoggerV2::init(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (initialized_.load(std::memory_order_acquire)) {
        shutdown();
    }

    global_level_.store(config.global_level, std::memory_order_relaxed);
    async_mode_ = config.async;

    sync_sinks_.clear();
    queue_.reset();

    if (async_mode_) {
        queue_.reset(new AsyncLogQueue(config.queue_capacity, config.overflow_policy));
        queue_->setFlushInterval(config.flush_interval_ms);
    }

    // Build sinks from config.sinks (logger_config.h).
    for (size_t i = 0; i < config.sinks.size(); ++i) {
        const SinkConfig& sc = config.sinks[i];

        if (sc.type == "console") {
            FILE* stream = sc.use_stderr ? stderr : stdout;
            std::shared_ptr<ConsoleSink> console(new ConsoleSink(stream, sc.level));
            console->setUseColors(sc.use_colors);
            if (!sc.pattern.empty()) {
                console->setPattern(sc.pattern);
            }

            if (async_mode_ && queue_) {
                queue_->addSink(console);
            } else {
                sync_sinks_.push_back(console);
            }
            continue;
        }

        if (sc.type == "file") {
            if (sc.path.empty()) {
                continue;
            }
            std::shared_ptr<FileSink> file(new FileSink(sc.path, sc.level));
            if (!sc.pattern.empty()) {
                file->setPattern(sc.pattern);
            }

            if (async_mode_ && queue_) {
                queue_->addSink(file);
            } else {
                sync_sinks_.push_back(file);
            }
            continue;
        }

        // Unknown sink type: ignore for now.
    }

    if (async_mode_ && queue_) {
        queue_->start();
    }

    initialized_.store(true, std::memory_order_release);
}

void LoggerV2::shutdown() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!initialized_.load(std::memory_order_acquire)) {
        return;
    }

    if (queue_) {
        queue_->stop();
        queue_.reset();
    }

    sync_sinks_.clear();
    initialized_.store(false, std::memory_order_release);
}

void LoggerV2::ensureInitialized() {
    if (!initialized_.load(std::memory_order_acquire)) {
        init();
    }
}

void LoggerV2::dispatch(LogMessage&& msg) {
    if (!hasActiveSinks()) {
        return;
    }
    if (async_mode_ && queue_) {
        queue_->push(std::move(msg));
        return;
    }

    for (size_t i = 0; i < sync_sinks_.size(); ++i) {
        if (sync_sinks_[i] && sync_sinks_[i]->shouldLog(msg.level)) {
            sync_sinks_[i]->write(msg);
        }
    }
}

bool LoggerV2::hasActiveSinks() {
    if (async_mode_ && queue_) {
        return queue_->hasSinks();
    }
    return !sync_sinks_.empty();
}

void LoggerV2::addSink(std::shared_ptr<LogSink> sink) {
    ensureInitialized();
    if (!sink) {
        return;
    }

    if (async_mode_ && queue_) {
        queue_->addSink(sink);
    } else {
        sync_sinks_.push_back(sink);
    }
}

void LoggerV2::flush() {
    ensureInitialized();
    if (async_mode_ && queue_) {
        queue_->flushSinks();
        return;
    }

    for (size_t i = 0; i < sync_sinks_.size(); ++i) {
        if (sync_sinks_[i]) {
            sync_sinks_[i]->flush();
        }
    }
}

void LoggerV2::setPattern(const std::string& pattern) {
    ensureInitialized();
    if (async_mode_ && queue_) {
        queue_->setPattern(pattern);
        return;
    }

    for (size_t i = 0; i < sync_sinks_.size(); ++i) {
        if (sync_sinks_[i]) {
            sync_sinks_[i]->setPattern(pattern);
        }
    }
}

}  // namespace utils

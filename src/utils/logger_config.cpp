/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @Description: 日志系统配置实现
 */

#include "logger_config.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <algorithm>
#include <iomanip>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace utils {

// ============================================================================
// SinkConfig 实现
// ============================================================================

SinkConfig SinkConfig::fromJson(const std::string& json_str) {
    // 简化实现: 解析关键字段
    // 实际应该使用完整的JSON解析器
    SinkConfig config;
    
    // 查找type字段
    size_t type_pos = json_str.find("\"type\"");
    if (type_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', type_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                config.type = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
    }
    
    // 查找level字段
    size_t level_pos = json_str.find("\"level\"");
    if (level_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', level_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                std::string level_str = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
                config.level = stringToLogLevel(level_str);
            }
        }
    }
    
    // 查找pattern字段
    size_t pattern_pos = json_str.find("\"pattern\"");
    if (pattern_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', pattern_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                config.pattern = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
    }
    
    // 查找path字段
    size_t path_pos = json_str.find("\"path\"");
    if (path_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', path_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                config.path = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
    }
    
    // 查找use_colors字段
    size_t colors_pos = json_str.find("\"use_colors\"");
    if (colors_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', colors_pos);
        size_t value_start = json_str.find_first_not_of(" \t\n\r", colon_pos + 1);
        if (value_start != std::string::npos) {
            if (json_str.substr(value_start, 4) == "true") {
                config.use_colors = true;
            } else if (json_str.substr(value_start, 5) == "false") {
                config.use_colors = false;
            }
        }
    }
    
    return config;
}

std::string SinkConfig::toString() const {
    std::stringstream ss;
    ss << "SinkConfig{type=" << type
       << ", level=" << logLevelToString(level)
       << ", pattern=" << (pattern.empty() ? "(default)" : pattern.substr(0, 20) + "...")
       << ", path=" << (path.empty() ? "(none)" : path)
       << ", use_colors=" << (use_colors ? "true" : "false")
       << ", max_size_mb=" << max_size_mb
       << ", max_files=" << max_files
       << "}";
    return ss.str();
}

// ============================================================================
// LoggerConfig 实现
// ============================================================================

LoggerConfig LoggerConfig::defaultConfig() {
    LoggerConfig config;
    
    // 添加默认控制台sink
    SinkConfig console_sink;
    console_sink.type = "console";
    console_sink.level = LogLevel::INFO;
    console_sink.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v";
    console_sink.use_colors = true;
    config.sinks.push_back(console_sink);
    
    return config;
}

LoggerConfig LoggerConfig::fromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // 文件不存在, 返回默认配置
        return defaultConfig();
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return fromJson(buffer.str());
}

LoggerConfig LoggerConfig::fromJson(const std::string& json_str) {
    LoggerConfig config = defaultConfig();
    
    // 简化实现: 解析关键字段
    // 实际应该使用完整的JSON解析器
    
    // 查找global_level字段
    size_t level_pos = json_str.find("\"global_level\"");
    if (level_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', level_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                std::string level_str = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
                config.global_level = stringToLogLevel(level_str);
            }
        }
    }
    
    // 查找async字段
    size_t async_pos = json_str.find("\"async\"");
    if (async_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', async_pos);
        size_t value_start = json_str.find_first_not_of(" \t\n\r", colon_pos + 1);
        if (value_start != std::string::npos) {
            if (json_str.substr(value_start, 4) == "true") {
                config.async = true;
            } else if (json_str.substr(value_start, 5) == "false") {
                config.async = false;
            }
        }
    }
    
    // 查找queue_capacity字段
    size_t capacity_pos = json_str.find("\"queue_capacity\"");
    if (capacity_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', capacity_pos);
        size_t value_start = json_str.find_first_not_of(" \t\n\r", colon_pos + 1);
        if (value_start != std::string::npos) {
            std::string value_str;
            while (value_start < json_str.size() && 
                   (std::isdigit(json_str[value_start]) || json_str[value_start] == '-')) {
                value_str += json_str[value_start++];
            }
            if (!value_str.empty()) {
                config.queue_capacity = std::stoul(value_str);
            }
        }
    }

    size_t overflow_pos = json_str.find("\"overflow_policy\"");
    if (overflow_pos != std::string::npos) {
        size_t colon_pos = json_str.find(':', overflow_pos);
        size_t quote1 = json_str.find('"', colon_pos);
        if (quote1 != std::string::npos) {
            size_t quote2 = json_str.find('"', quote1 + 1);
            if (quote2 != std::string::npos) {
                const std::string overflow = json_str.substr(quote1 + 1, quote2 - quote1 - 1);
                if (overflow == "block") {
                    config.overflow_policy = LogOverflowPolicy::Block;
                } else if (overflow == "drop_newest") {
                    config.overflow_policy = LogOverflowPolicy::DropNewest;
                } else if (overflow == "drop_if_below_error") {
                    config.overflow_policy = LogOverflowPolicy::DropIfBelowError;
                }
            }
        }
    }
    
    // 查找sinks数组
    size_t sinks_pos = json_str.find("\"sinks\"");
    if (sinks_pos != std::string::npos) {
        size_t bracket_pos = json_str.find('[', sinks_pos);
        if (bracket_pos != std::string::npos) {
            config.sinks.clear();  // 清除默认sink
            
            size_t end_pos = json_str.find(']', bracket_pos);
            if (end_pos != std::string::npos) {
                // 简化实现: 查找每个sink对象
                size_t obj_start = bracket_pos;
                while ((obj_start = json_str.find('{', obj_start)) != std::string::npos && 
                       obj_start < end_pos) {
                    size_t obj_end = json_str.find('}', obj_start);
                    if (obj_end != std::string::npos && obj_end < end_pos) {
                        std::string sink_json = json_str.substr(obj_start, obj_end - obj_start + 1);
                        config.sinks.push_back(SinkConfig::fromJson(sink_json));
                        obj_start = obj_end + 1;
                    } else {
                        break;
                    }
                }
            }
        }
    }
    
    return config;
}

LoggerConfig LoggerConfig::fromEnvironment() {
    LoggerConfig config = defaultConfig();
    
    // 从环境变量读取全局日志级别
    const char* level = std::getenv("LOG_LEVEL");
    if (level) {
        config.global_level = stringToLogLevel(level);
    }
    
    // 异步模式
    const char* async = std::getenv("LOG_ASYNC");
    if (async) {
        config.async = (std::strcmp(async, "1") == 0 || 
                       std::strcmp(async, "true") == 0 ||
                       std::strcmp(async, "yes") == 0);
    }
    
    // 队列容量
    const char* queue_size = std::getenv("LOG_QUEUE_SIZE");
    if (queue_size) {
        try {
            config.queue_capacity = std::stoul(queue_size);
        } catch (...) {
            // 忽略转换错误
        }
    }

    const char* overflow_policy = std::getenv("LOG_OVERFLOW_POLICY");
    if (overflow_policy) {
        const std::string policy = overflow_policy;
        if (policy == "block") {
            config.overflow_policy = LogOverflowPolicy::Block;
        } else if (policy == "drop_newest") {
            config.overflow_policy = LogOverflowPolicy::DropNewest;
        } else if (policy == "drop_if_below_error") {
            config.overflow_policy = LogOverflowPolicy::DropIfBelowError;
        }
    }
    
    // 刷新间隔
    const char* flush_interval = std::getenv("LOG_FLUSH_INTERVAL");
    if (flush_interval) {
        try {
            config.flush_interval_ms = std::stoi(flush_interval);
        } catch (...) {
            // 忽略转换错误
        }
    }
    
    // 日志文件路径
    const char* log_file = std::getenv("LOG_FILE");
    if (log_file) {
        // 添加文件sink
        SinkConfig file_sink;
        file_sink.type = "file";
        file_sink.level = config.global_level;
        file_sink.path = log_file;
        file_sink.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v";
        config.sinks.push_back(file_sink);
    }
    
    // 日志模式
    const char* pattern = std::getenv("LOG_PATTERN");
    if (pattern) {
        // 更新所有sink的模式
        for (auto& sink : config.sinks) {
            sink.pattern = pattern;
        }
    }
    
    return config;
}

LoggerConfig LoggerConfig::merge(const LoggerConfig& base, const LoggerConfig& override) {
    LoggerConfig result = base;
    
    // 合并全局设置
    if (override.global_level != LogLevel::INFO) {
        result.global_level = override.global_level;
    }
    
    // 合并异步设置
    result.async = override.async;
    
    // 合并队列容量
    if (override.queue_capacity != 8192) {
        result.queue_capacity = override.queue_capacity;
    }
    
    // 合并刷新间隔
    if (override.flush_interval_ms != 1000) {
        result.flush_interval_ms = override.flush_interval_ms;
    }

    if (override.overflow_policy != LogOverflowPolicy::DropIfBelowError) {
        result.overflow_policy = override.overflow_policy;
    }
    
    // 合并sink配置
    if (!override.sinks.empty()) {
        result.sinks = override.sinks;
    }
    
    return result;
}

std::string LoggerConfig::toString() const {
    std::stringstream ss;
    ss << "LoggerConfig{"
       << "global_level=" << logLevelToString(global_level)
       << ", async=" << (async ? "true" : "false")
       << ", queue_capacity=" << queue_capacity
       << ", flush_interval_ms=" << flush_interval_ms
       << ", overflow_policy="
       << (overflow_policy == LogOverflowPolicy::Block ? "block" :
           overflow_policy == LogOverflowPolicy::DropNewest ? "drop_newest" :
           "drop_if_below_error")
       << ", sinks=[";
    
    for (size_t i = 0; i < sinks.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << sinks[i].toString();
    }
    
    ss << "]}";
    return ss.str();
}

bool LoggerConfig::validate() const {
    // 检查日志级别有效性
    if (global_level < LogLevel::TRACE || global_level > LogLevel::OFF) {
        return false;
    }
    
    // 检查队列容量
    if (queue_capacity == 0) {
        return false;
    }
    
    // 检查刷新间隔
    if (flush_interval_ms <= 0) {
        return false;
    }
    
    // 检查sink配置
    for (const auto& sink : sinks) {
        if (sink.type.empty()) {
            return false;
        }
        
        if (sink.level < LogLevel::TRACE || sink.level > LogLevel::OFF) {
            return false;
        }
        
        if (sink.type == "file" && sink.path.empty()) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// ConfigManager 实现
// ============================================================================

bool ConfigManager::load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LoggerConfig new_config;
    if (!parseJsonFile(filename, new_config)) {
        return false;
    }
    
    config_ = new_config;
    config_file_ = filename;
    last_modified_ = getFileModTime(filename);
    
    return true;
}

bool ConfigManager::loadFromJson(const std::string& json_str) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = LoggerConfig::fromJson(json_str);
    config_file_.clear();
    last_modified_ = 0;
    
    return config_.validate();
}

void ConfigManager::updateConfig(const LoggerConfig& new_config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (new_config.validate()) {
        config_ = new_config;
        
        // 触发变更回调
        if (change_callback_) {
            change_callback_(config_);
        }
    }
}

bool ConfigManager::checkForChanges() {
    if (config_file_.empty()) {
        return false;
    }
    
    std::time_t current_mod_time = getFileModTime(config_file_);
    if (current_mod_time == 0) {
        // 文件不存在或无法访问
        return false;
    }
    
    if (current_mod_time > last_modified_) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        LoggerConfig new_config;
        if (parseJsonFile(config_file_, new_config)) {
            config_ = new_config;
            last_modified_ = current_mod_time;
            
            // 触发变更回调
            if (change_callback_) {
                change_callback_(config_);
            }
            
            return true;
        }
    }
    
    return false;
}

void ConfigManager::setChangeCallback(std::function<void(const LoggerConfig&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_callback_ = std::move(callback);
}

bool ConfigManager::save(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    // 生成JSON格式的配置
    file << "{\n";
    file << "  \"global_level\": \"" << logLevelToString(config_.global_level) << "\",\n";
    file << "  \"async\": " << (config_.async ? "true" : "false") << ",\n";
    file << "  \"queue_capacity\": " << config_.queue_capacity << ",\n";
    file << "  \"flush_interval_ms\": " << config_.flush_interval_ms << ",\n";
    file << "  \"overflow_policy\": \""
         << (config_.overflow_policy == LogOverflowPolicy::Block ? "block" :
             config_.overflow_policy == LogOverflowPolicy::DropNewest ? "drop_newest" :
             "drop_if_below_error")
         << "\",\n";
    file << "  \"sinks\": [\n";
    
    for (size_t i = 0; i < config_.sinks.size(); ++i) {
        const auto& sink = config_.sinks[i];
        file << "    {\n";
        file << "      \"type\": \"" << sink.type << "\",\n";
        file << "      \"level\": \"" << logLevelToString(sink.level) << "\",\n";
        
        if (!sink.pattern.empty()) {
            file << "      \"pattern\": \"" << sink.pattern << "\",\n";
        }
        
        if (sink.type == "console") {
            file << "      \"use_colors\": " << (sink.use_colors ? "true" : "false") << "\n";
        } else if (sink.type == "file") {
            file << "      \"path\": \"" << sink.path << "\",\n";
            file << "      \"max_size_mb\": " << sink.max_size_mb << ",\n";
            file << "      \"max_files\": " << sink.max_files << "\n";
        }
        
        file << "    }";
        if (i < config_.sinks.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    
    return true;
}

bool ConfigManager::parseJsonFile(const std::string& filename, LoggerConfig& config) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    config = LoggerConfig::fromJson(buffer.str());
    
    return config.validate();
}

std::time_t ConfigManager::getFileModTime(const std::string& filename) const {
    struct stat file_stat;
    if (stat(filename.c_str(), &file_stat) != 0) {
        return 0;
    }
    
#ifdef _WIN32
    return file_stat.st_mtime;
#else
    return file_stat.st_mtim.tv_sec;
#endif
}

// ============================================================================
// config_utils 实现
// ============================================================================

namespace config_utils {

LogLevel parseLogLevel(const std::string& level_str) {
    return stringToLogLevel(level_str);
}

bool parseBool(const std::string& bool_str) {
    std::string lower;
    lower.reserve(bool_str.size());
    std::transform(bool_str.begin(), bool_str.end(), std::back_inserter(lower),
                   [](unsigned char c) { return std::tolower(c); });
    
    return (lower == "true" || lower == "yes" || lower == "1" || lower == "on");
}

size_t parseSize(const std::string& size_str) {
    if (size_str.empty()) {
        return 0;
    }
    
    size_t result = 0;
    char* endptr = nullptr;
    result = std::strtoull(size_str.c_str(), &endptr, 10);
    
    if (endptr && *endptr != '\0') {
        std::string unit(endptr);
        std::transform(unit.begin(), unit.end(), unit.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        
        if (unit == "kb" || unit == "k") {
            result *= 1024;
        } else if (unit == "mb" || unit == "m") {
            result *= 1024 * 1024;
        } else if (unit == "gb" || unit == "g") {
            result *= 1024 * 1024 * 1024;
        } else if (unit == "tb" || unit == "t") {
            result *= 1024ULL * 1024 * 1024 * 1024;
        }
    }
    
    return result;
}

std::string getEnv(const std::string& name, const std::string& default_value) {
    const char* value = std::getenv(name.c_str());
    if (value) {
        return value;
    }
    return default_value;
}

std::string expandPath(const std::string& path) {
    std::string result = path;
    
    // 展开环境变量
    size_t start_pos = 0;
    while ((start_pos = result.find("${", start_pos)) != std::string::npos) {
        size_t end_pos = result.find("}", start_pos);
        if (end_pos == std::string::npos) {
            break;
        }
        
        std::string var_name = result.substr(start_pos + 2, end_pos - start_pos - 2);
        const char* var_value = std::getenv(var_name.c_str());
        
        if (var_value) {
            result.replace(start_pos, end_pos - start_pos + 1, var_value);
            start_pos += std::strlen(var_value);
        } else {
            start_pos = end_pos + 1;
        }
    }
    
    // 展开用户目录 (~)
    if (!result.empty() && result[0] == '~') {
#ifdef _WIN32
        // Windows: 使用USERPROFILE环境变量
        const char* home = std::getenv("USERPROFILE");
        if (!home) {
            home = std::getenv("HOMEDRIVE");
            const char* homepath = std::getenv("HOMEPATH");
            if (home && homepath) {
                std::string win_home = std::string(home) + homepath;
                home = win_home.c_str();
            }
        }
#else
        // Unix-like: 使用HOME环境变量或getpwuid
        const char* home = std::getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) {
                home = pw->pw_dir;
            }
        }
#endif
        
        if (home) {
            result.replace(0, 1, home);
        }
    }
    
    return result;
}

bool ensureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    std::string dir_path = path;
    
    // 如果是文件路径, 提取目录部分
    size_t slash_pos = dir_path.find_last_of("/\\");
    if (slash_pos != std::string::npos) {
        dir_path = dir_path.substr(0, slash_pos);
    }
    
    if (dir_path.empty()) {
        return true;  // 当前目录
    }
    
    // 检查目录是否存在
    struct stat info;
    if (stat(dir_path.c_str(), &info) == 0) {
        return (info.st_mode & S_IFDIR) != 0;
    }
    
    // 递归创建目录
    size_t pos = 0;
    std::string create_path;
    
#ifdef _WIN32
    // Windows: 处理驱动器号
    if (dir_path.size() >= 2 && dir_path[1] == ':') {
        pos = 2;
        create_path = dir_path.substr(0, 2);
    }
#endif
    
    while (pos < dir_path.size()) {
        size_t next_pos = dir_path.find_first_of("/\\", pos);
        if (next_pos == std::string::npos) {
            next_pos = dir_path.size();
        }
        
        std::string component = dir_path.substr(pos, next_pos - pos);
        if (!component.empty()) {
            if (!create_path.empty()) {
                create_path += '/';
            }
            create_path += component;
            
            // 创建目录
            if (mkdir(create_path.c_str(), 0755) != 0) {
                if (errno != EEXIST) {
                    return false;
                }
            }
        }
        
        pos = next_pos + 1;
    }
    
    return true;
}

} // namespace config_utils

} // namespace utils

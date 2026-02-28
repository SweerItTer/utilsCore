/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @Description: 日志系统配置管理
 */

#ifndef LOGGER_CONFIG_H
#define LOGGER_CONFIG_H

#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include "logger_v2.h"

namespace utils {

/**
 * @brief 单个Sink的配置
 */
struct SinkConfig {
    std::string type;                     ///< sink类型: "console", "file", "rotating_file"
    LogLevel level = LogLevel::INFO;      ///< 该sink的最小日志级别
    std::string pattern;                  ///< 输出模式字符串
    
    // 控制台sink特定配置
    bool use_colors = true;               ///< 是否使用颜色
    bool use_stderr = false;              ///< 是否输出到stderr
    
    // 文件sink特定配置
    std::string path;                     ///< 文件路径
    size_t max_size_mb = 100;             ///< 最大文件大小(MB)
    size_t max_files = 10;                ///< 最大文件数量
    bool rotate_on_open = false;          ///< 打开时是否轮转
    
    // 从JSON对象解析
    static SinkConfig fromJson(const std::string& json_str);
    
    // 转换为字符串(用于调试)
    std::string toString() const;
};

/**
 * @brief 完整的日志系统配置
 */
struct LoggerConfig {
    LogLevel global_level = LogLevel::INFO;   ///< 全局日志级别
    bool async = true;                        ///< 是否使用异步日志
    size_t queue_capacity = 8192;             ///< 异步队列容量
    int flush_interval_ms = 1000;             ///< 刷新间隔(毫秒)
    std::vector<SinkConfig> sinks;            ///< 输出sink配置
    
    // 默认配置
    static LoggerConfig defaultConfig();
    
    // 从JSON文件加载配置
    static LoggerConfig fromFile(const std::string& filename);
    
    // 从JSON字符串加载配置
    static LoggerConfig fromJson(const std::string& json_str);
    
    // 从环境变量加载配置
    static LoggerConfig fromEnvironment();
    
    // 合并多个配置源
    static LoggerConfig merge(const LoggerConfig& base, const LoggerConfig& override);
    
    // 转换为字符串(用于调试)
    std::string toString() const;
    
    // 验证配置有效性
    bool validate() const;
};

/**
 * @brief 配置管理器(支持热重载)
 */
class ConfigManager {
public:
    ConfigManager() = default;
    
    /**
     * @brief 加载配置文件
     */
    bool load(const std::string& filename);
    
    /**
     * @brief 从JSON字符串加载配置
     */
    bool loadFromJson(const std::string& json_str);
    
    /**
     * @brief 获取当前配置
     */
    const LoggerConfig& getConfig() const { return config_; }
    
    /**
     * @brief 更新配置(线程安全)
     */
    void updateConfig(const LoggerConfig& new_config);
    
    /**
     * @brief 检查配置是否已更改(用于热重载)
     */
    bool checkForChanges();
    
    /**
     * @brief 设置配置变更回调
     */
    void setChangeCallback(std::function<void(const LoggerConfig&)> callback);
    
    /**
     * @brief 保存当前配置到文件
     */
    bool save(const std::string& filename) const;
    
private:
    LoggerConfig config_;
    std::string config_file_;
    std::time_t last_modified_ = 0;
    std::function<void(const LoggerConfig&)> change_callback_;
    mutable std::mutex mutex_;
    
    bool parseJsonFile(const std::string& filename, LoggerConfig& config);
    std::time_t getFileModTime(const std::string& filename) const;
};

/**
 * @brief 配置工具函数
 */
namespace config_utils {
    
    /**
     * @brief 解析日志级别字符串
     */
    LogLevel parseLogLevel(const std::string& level_str);
    
    /**
     * @brief 解析布尔值字符串
     */
    bool parseBool(const std::string& bool_str);
    
    /**
     * @brief 解析大小字符串(如 "100MB", "1GB")
     */
    size_t parseSize(const std::string& size_str);
    
    /**
     * @brief 获取环境变量值, 带默认值
     */
    std::string getEnv(const std::string& name, const std::string& default_value = "");
    
    /**
     * @brief 展开路径中的环境变量和用户目录
     */
    std::string expandPath(const std::string& path);
    
    /**
     * @brief 确保目录存在
     */
    bool ensureDirectory(const std::string& path);
    
} // namespace config_utils

} // namespace utils

#endif // LOGGER_CONFIG_H

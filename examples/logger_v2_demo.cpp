/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @Description: LoggerV2 使用例程
 *
 * 目标: 给出一个最小但“看得出效果”的 LoggerV2 使用示例: 
 * - 通过 LoggerConfig 初始化
 * - 基础日志 + 结构化字段
 * - 动态调整日志级别并观察过滤效果
 */

#include "logger_config.h"
#include <iostream>

using namespace utils;

int main() {
    std::cout << "========================================\n";
    std::cout << "        LoggerV2 Demo (Example)\n";
    std::cout << "========================================\n";

    LoggerConfig config = LoggerConfig::defaultConfig();
    config.global_level = LogLevel::INFO;
    // 为了让示例输出顺序稳定，这里使用同步模式；生产环境可开启 async。
    config.async = false;

    if (!config.sinks.empty()) {
        // 简化输出格式，让示例更易读
        config.sinks[0].pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] %v";
    }

    LoggerV2::init(config);
    std::cout << "✓ init ok\n";

    LOG_INFO("Hello LoggerV2");
    LOG_WARN("This is a warning");

    LogFields fields = {
        {"module", "demo"},
        {"user", "alice"},
        {"action", "login"},
    };
    LOG_INFO_FIELDS(fields, "Structured log with fields");

    std::cout << "\n-- level control --\n";
    LoggerV2::setLevel(LogLevel::ERROR);
    LOG_INFO("This INFO should NOT appear (level=ERROR)");
    LOG_ERROR("This ERROR should appear");

    LoggerV2::setLevel(LogLevel::INFO);
    LOG_INFO("Back to INFO");

    LoggerV2::flush();
    LoggerV2::shutdown();
    std::cout << "✓ shutdown ok\n";

    return 0;
}

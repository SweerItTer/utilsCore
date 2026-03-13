# 生产级日志系统设计文档

## 1. 设计目标

### 1.1 核心需求
- 支持多级别日志：TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- 结构化日志输出（键值对）
- 异步日志写入，避免阻塞业务线程
- 线程安全，高性能
- 可配置的输出目标（控制台、文件、syslog、网络）
- 日志轮转和归档
- 编译时日志级别过滤

### 1.2 非功能需求
- 零开销日志级别检查（编译时优化）
- 低延迟：异步日志队列
- 高吞吐：批量写入
- 内存安全：RAII资源管理
- 易于集成：头文件库，无外部依赖

## 2. 架构设计

### 2.1 核心组件

```
┌─────────────────────────────────────────────────────────┐
│                    Logger Facade                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │   TRACE()   │  │   DEBUG()   │  │    INFO()   │ ... │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                 Logger Core (Singleton)                  │
│  ┌─────────────────────────────────────────────────┐    │
│  │  Log Level Filter  │  Log Queue (Async)         │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────┐
│                    Sink Manager                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │ ConsoleSink │  │  FileSink   │  │ SyslogSink  │ ... │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### 2.2 日志级别定义

```cpp
enum class LogLevel : uint8_t {
    TRACE = 0,    // 最详细的调试信息
    DEBUG = 1,    // 调试信息
    INFO  = 2,    // 常规信息
    WARN  = 3,    // 警告信息
    ERROR = 4,    // 错误信息
    FATAL = 5,    // 致命错误
    OFF   = 6     // 关闭所有日志
};
```

### 2.3 日志消息结构

```cpp
struct LogMessage {
    LogLevel level;                    // 日志级别
    std::chrono::system_clock::time_point timestamp; // 时间戳
    std::thread::id thread_id;         // 线程ID
    std::string_view file;             // 源文件
    int line;                          // 行号
    std::string_view function;         // 函数名
    std::string message;               // 日志消息
    std::unordered_map<std::string, std::string> fields; // 结构化字段
};
```

## 3. 接口设计

### 3.1 日志门面宏

```cpp
// 基础日志宏
#define LOG_TRACE(...)    Logger::log(LogLevel::TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(...)    Logger::log(LogLevel::DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)     Logger::log(LogLevel::INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)     Logger::log(LogLevel::WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)    Logger::log(LogLevel::ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATAL(...)    Logger::log(LogLevel::FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

// 结构化日志宏
#define LOG_TRACE_FIELDS(fields, ...) Logger::logWithFields(LogLevel::TRACE, __FILE__, __LINE__, __func__, fields, __VA_ARGS__)
```

### 3.2 核心接口

```cpp
class Logger {
public:
    // 初始化配置
    static void init(const LoggerConfig& config);
    
    // 基础日志接口
    template<typename... Args>
    static void log(LogLevel level, const char* file, int line, const char* function,
                   fmt::format_string<Args...> fmt, Args&&... args);
    
    // 结构化日志接口
    template<typename... Args>
    static void logWithFields(LogLevel level, const char* file, int line, const char* function,
                             const LogFields& fields, fmt::format_string<Args...> fmt, Args&&... args);
    
    // 设置日志级别
    static void setLevel(LogLevel level);
    
    // 刷新日志缓冲区
    static void flush();
    
    // 关闭日志系统
    static void shutdown();
};
```

## 4. 实现细节

### 4.1 异步日志队列

使用无锁队列实现高性能异步日志：

```cpp
class AsyncLogQueue {
public:
    bool push(LogMessage&& msg);
    bool pop(LogMessage& msg);
    size_t size() const;
    
private:
    moodycamel::ConcurrentQueue<LogMessage> queue_;
    std::atomic<bool> running_{true};
    std::thread worker_thread_;
};
```

### 4.2 输出Sink抽象

```cpp
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const LogMessage& msg) = 0;
    virtual void flush() = 0;
    virtual void setPattern(const std::string& pattern) = 0;
};

class ConsoleSink : public LogSink;
class FileSink : public LogSink;
class RotatingFileSink : public LogSink;
class SyslogSink : public LogSink;
```

### 4.3 日志轮转策略

```cpp
class LogRotationPolicy {
public:
    virtual bool shouldRotate(const std::string& current_file, size_t current_size) = 0;
    virtual std::string getNextFileName() = 0;
};

class SizeBasedRotation : public LogRotationPolicy;
class TimeBasedRotation : public LogRotationPolicy;
```

## 5. 配置系统

### 5.1 配置文件格式（JSON）

```json
{
    "log_level": "INFO",
    "async": true,
    "queue_size": 8192,
    "flush_interval_ms": 1000,
    "sinks": [
        {
            "type": "console",
            "pattern": "[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v"
        },
        {
            "type": "file",
            "path": "logs/app.log",
            "rotation": {
                "max_size_mb": 100,
                "max_files": 10
            }
        }
    ]
}
```

### 5.2 环境变量支持

```bash
export LOG_LEVEL=DEBUG
export LOG_FILE=/var/log/app.log
export LOG_ASYNC=true
```

## 6. 性能优化

### 6.1 编译时优化

```cpp
// 编译时日志级别检查
template<LogLevel Level>
struct LogEnabled {
    static constexpr bool value = 
        Level >= CURRENT_LOG_LEVEL && 
        Logger::isInitialized();
};

// 使用示例
if constexpr (LogEnabled<LogLevel::DEBUG>::value) {
    LOG_DEBUG("Debug message: {}", expensiveComputation());
}
```

### 6.2 内存池优化

使用对象池重用LogMessage对象，避免频繁内存分配。

### 6.3 批量写入

异步工作线程批量处理日志消息，减少I/O操作。

## 7. 集成计划

### 7.1 第一阶段：核心实现
1. 实现Logger核心类
2. 实现ConsoleSink和FileSink
3. 实现异步日志队列
4. 创建配置系统

### 7.2 第二阶段：高级功能
1. 实现日志轮转
2. 添加结构化日志支持
3. 实现syslog和网络sink
4. 添加性能监控

### 7.3 第三阶段：集成和测试
1. 替换现有日志调用
2. 性能测试和优化
3. 文档编写
4. 示例代码

## 8. 向后兼容性

提供兼容层，支持现有代码：

```cpp
// 兼容现有Logger::log调用
namespace compatibility {
    void log(FILE* stream, const char* format, ...);
}
```

## 9. 测试策略

### 9.1 单元测试
- 日志级别过滤
- 异步队列操作
- Sink写入测试
- 配置加载测试

### 9.2 性能测试
- 吞吐量测试（日志/秒）
- 延迟测试
- 内存使用测试
- 多线程并发测试

### 9.3 集成测试
- 与现有代码集成测试
- 长时间运行稳定性测试
- 日志轮转测试
- 配置热重载测试

## 10. 风险评估和缓解

### 10.1 风险：性能下降
- 缓解：使用异步队列，编译时优化

### 10.2 风险：内存泄漏
- 缓解：使用RAII，智能指针，内存池

### 10.3 风险：日志丢失
- 缓解：同步刷新机制，优雅关闭

### 10.4 风险：配置错误
- 缓解：配置验证，默认值，错误提示
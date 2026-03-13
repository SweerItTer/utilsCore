# 日志系统迁移指南

## 概述

本指南描述了如何将项目从旧的日志系统迁移到新的生产级日志系统（LoggerV2）。我们已经完成了rgaProcessor.cpp的迁移作为示例。

## 新旧日志系统对比

### 旧日志系统 (logger.h)
- 简单的`Logger::log()`静态方法
- 支持printf风格格式化
- 基本时间戳
- 全局`LogFlag`控制开关
- 文件和控制台输出

### 新日志系统 (logger_v2.h)
- 多级别日志：TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- 结构化日志支持
- 异步日志写入
- 线程安全
- 可配置输出目标
- 编译时日志级别过滤
- 零开销日志级别检查

## 迁移步骤

### 1. 更新包含语句
将：
```cpp
#include "logger.h"
```
替换为：
```cpp
#include "logger_v2.h"
```

### 2. 替换日志调用

#### 2.1 Logger::log 调用
将：
```cpp
Logger::log(stdout, "[Module] Message: %s", arg);
```
替换为：
```cpp
LOG_INFO("Message: %s", arg);
```

将：
```cpp
Logger::log(stderr, "[Module] Error: %s", arg);
```
替换为：
```cpp
LOG_ERROR("Error: %s", arg);
```

#### 2.2 fprintf 调用
将：
```cpp
fprintf(stderr, "[Module] Warning: %s\n", message);
```
替换为：
```cpp
LOG_WARN("%s", message);
```

将：
```cpp
fprintf(stdout, "[Module] Info: %s\n", message);
```
替换为：
```cpp
LOG_INFO("%s", message);
```

#### 2.3 不存在的LOG_*宏
如果代码中使用了不存在的LOG_*宏（如LOG_INFO, LOG_ERROR等），这些宏现在在新系统中已正确定义，可以直接使用。

### 3. 日志级别选择指南

| 场景 | 推荐级别 | 示例 |
|------|----------|------|
| 详细的调试信息 | TRACE | `LOG_TRACE("Entering function: %s", __func__)` |
| 开发调试信息 | DEBUG | `LOG_DEBUG("Variable value: %d", value)` |
| 常规运行信息 | INFO | `LOG_INFO("Service started on port %d", port)` |
| 可能的问题 | WARN | `LOG_WARN("Resource usage high: %d%%", usage)` |
| 错误但可恢复 | ERROR | `LOG_ERROR("Failed to connect: %s", error)` |
| 致命错误 | FATAL | `LOG_FATAL("System integrity compromised")` |

### 4. 结构化日志

新系统支持结构化日志：
```cpp
utils::LogFields fields = {
    {"user_id", "12345"},
    {"session_id", "abcde"},
    {"action", "login"}
};
LOG_INFO_FIELDS(fields, "User action performed");
```

### 5. 条件日志

使用条件日志宏避免不必要的计算：
```cpp
LOG_DEBUG_IF(config.debug_mode, "Debug info: %s", expensiveComputation());
```

### 6. 一次性日志

对于只应记录一次的日志：
```cpp
LOG_INFO_ONCE("This message will only appear once");
```

## 需要迁移的文件列表

根据扫描结果，以下文件使用了旧的日志系统，需要迁移：

### src/utils/ 目录
1. `dma/dmaBuffer.cpp`
2. `drm/drmLayer.cpp`
3. `drm/planesCompositor.cpp`
4. `mouse/watcher.cpp`
5. `mpp/encoderContext.cpp`
6. `mpp/encoderCore.cpp`
7. `mpp/jpegEncoder.cpp`
8. `mpp/streamWriter.cpp`
9. `net/commandHandler.cpp`
10. `net/socketConnection.cpp`
11. `net/tcpServer.cpp`
12. `v4l2/cameraController.cpp`
13. `v4l2param/paramLogger.cpp`
14. `v4l2param/paramProcessor.cpp`

### include/utils/ 目录
1. `mpp/encoderContext.h`
2. `net/tcpServer.h`
3. `v4l2/v4l2Exception.h`

## 迁移优先级建议

1. **高优先级**：核心模块和频繁使用的组件
   - `net/tcpServer.cpp` - 网络服务核心
   - `mpp/encoderContext.cpp` - 视频编码核心
   - `v4l2/cameraController.cpp` - 摄像头控制

2. **中优先级**：工具类和辅助模块
   - `dma/dmaBuffer.cpp` - DMA缓冲区管理
   - `drm/*.cpp` - DRM显示相关
   - `v4l2param/*.cpp` - 参数处理

3. **低优先级**：辅助工具和边缘模块
   - `mouse/watcher.cpp` - 鼠标监控
   - 其他工具类

## 测试验证

迁移每个文件后，应进行以下测试：

1. **编译测试**：确保代码能够编译通过
2. **功能测试**：运行相关功能测试
3. **日志输出测试**：验证日志级别和格式正确
4. **性能测试**：确保异步日志不影响性能

## 向后兼容性

### 兼容层
我们提供了`logging_compat.h`作为兼容层，但建议直接迁移到新API。

### 环境变量
新系统支持以下环境变量：
- `LOG_LEVEL` - 设置全局日志级别
- `LOG_ASYNC` - 启用/禁用异步日志
- `LOG_FILE` - 指定日志文件路径
- `LOG_PATTERN` - 设置日志格式模式

## 常见问题

### Q1: 迁移后日志不输出？
A: 确保调用了`utils::LoggerV2::init()`初始化日志系统，或使用默认配置自动初始化。

### Q2: 如何设置日志级别？
A: 使用`utils::LoggerV2::setLevel(utils::LogLevel::DEBUG)`或通过环境变量`LOG_LEVEL=DEBUG`。

### Q3: 异步日志消息丢失？
A: 在程序退出前调用`utils::LoggerV2::shutdown()`确保所有日志消息被刷新。

### Q4: 如何添加自定义输出目标？
A: 创建自定义`LogSink`类并调用`utils::LoggerV2::addSink()`。

## 已完成迁移

✅ `src/utils/rga/rgaProcessor.cpp` - 已完成迁移，作为示例参考

## 下一步计划

1. 创建自动化迁移脚本
2. 逐步迁移高优先级文件
3. 更新项目文档
4. 性能基准测试
5. 集成到CI/CD流程

## 参考资料

- [新日志系统设计文档](logger_design.md)
- [rgaProcessor.cpp迁移示例](../src/utils/rga/rgaProcessor.cpp)
- [LoggerV2 API文档](../include/utils/logger_v2.h)
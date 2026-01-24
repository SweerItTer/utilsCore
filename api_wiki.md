# utilsCore 项目索引

> 本文档提供项目所有模块、类、关键函数的快速索引，便于代码检索和定位。

## 📑 目录

- [Utils 模块](#utils-模块)

---

## Utils 模块

### 📁 路径
- 头文件：`include/utils/`
- 源文件：`src/utils/`

### 🏗️ 子模块

#### 线程与并发工具

**asyncThreadPool**
- 文件：`include/utils/asyncThreadPool.h`
- 功能：异步线程池，基于 `std::packaged_task` 和 `std::future`
- 关键方法：`enqueue(F&& f, Args&&... args)` - 入队任务

**concurrentqueue**

- 文件：`include/utils/concurrentqueue.h`
- 功能：MoodyCamel 无锁多生产者多消费者队列

**fixedSizePool**
- 文件：`include/utils/fixedSizePool.h`, `src/utils/fixedSizePool.cpp`
- 功能：高性能固定大小内存池
- 特点：TLS 缓存，缓存行对齐，ARM/x86 架构适配

**safeQueue**
- 文件：`include/utils/safeQueue.h`
- 功能：线程安全循环队列
- 特点：SFINAE 模板特化，多种溢出策略

**orderedQueue**
- 文件：`include/utils/orderedQueue.h`
- 功能：无锁环形缓冲有序队列
- 特点：帧序管理，CAS 原子操作，统计信息

**objectsPool**
- 文件：`include/utils/objectsPool.h`
- 功能：通用对象池
- 特点：工厂模式，条件变量，超时机制

---

#### 系统监控与日志

**logger**
- 文件：`include/utils/logger.h`, `src/utils/logger.cpp`
- 功能：线程安全日志记录器
- 特点：单例模式，毫秒级时间戳

**progressBar**
- 文件：`include/utils/progressBar.h`
- 功能：多进度条管理器
- 特点：ANSI 转义序列，线程安全

**threadPauser**
- 文件：`include/utils/threadPauser.h`, `src/utils/threadPauser.cpp`
- 功能：线程暂停控制器
- 特点：eventfd 内核级阻塞，零锁设计

**threadUtils**
- 文件：`include/utils/threadUtils.h`
- 功能：线程绑定和实时调度
- 特点：CPU 亲和性，FIFO 调度

**types**
- 文件：`include/utils/types.h`
- 功能：类型定义和别名
- 内容：FramePtr, FrameQueue

---

#### 设备监控

**udevMonitor**
- 文件：`include/utils/udevMonitor.h`, `src/utils/udevMonitor.cpp`
- 功能：Linux udev 事件监听器
- 特点：epoll + udev_monitor，回调注册

**fdWrapper**
- 文件：`include/utils/fdWrapper.h`
- 功能：RAII 文件描述符包装器
- 特点：移动语义，自动关闭

**fenceWatcher**
- 文件：`include/utils/fenceWatcher.h`, `src/utils/fenceWatcher.cpp`
- 功能：异步等待 GPU fence 完成
- 特点：epoll 监听，超时机制，单例模式

---

#### DMA 缓冲区管理

**dmaBuffer**
- 文件：`include/utils/dma/dmaBuffer.h`, `src/utils/dma/dmaBuffer.cpp`
- 功能：DRM DMA-BUF 管理
- 关键方法：`create()`, `importFromFD()`, `map()`, `unmap()`
- 特点：工厂方法，RAII 视图

**sharedBufferState**
- 文件：`include/utils/sharedBufferState.h`
- 功能：共享缓冲区状态管理
- 特点：所有权枚举，原子标志，移动语义

---

#### DRM 设备控制

**deviceController**
- 文件：`include/utils/drm/deviceController.h`, `src/utils/drm/deviceController.cpp`
- 功能：全局 DRM 设备管理器
- 特点：单例模式，资源缓存，热插拔支持

**drmBpp**
- 文件：`include/utils/drm/drmBpp.h`
- 功能：DRM 格式 bpp 计算和多平面信息
- 特点：V4L2 ↔ DRM 格式转换

**drmLayer**
- 文件：`include/utils/drm/drmLayer.h`, `src/utils/drm/drmLayer.cpp`
- 功能：DRM 图层抽象
- 特点：属性设置，FB 缓存，Fence 同步

**planesCompositor**
- 文件：`include/utils/drm/planesCompositor.h`, `src/utils/drm/planesCompositor.cpp`
- 功能：DRM 平面合成器
- 特点：原子提交，图层管理，属性缓存

---

#### 鼠标监控

**watcher**
- 文件：`include/utils/mouse/watcher.h`, `src/utils/mouse/watcher.cpp`
- 功能：Linux 鼠标事件监听器
- 特点：双缓冲，序列号，回调注册，设备热插拔

---

#### MPP 编码

**encoderContext**
- 文件：`include/utils/mpp/encoderContext.h`, `src/utils/mpp/encoderContext.cpp`
- 功能：Rockchip MPP 编码器上下文管理
- 特点：完整配置，动态重配，FFmpeg 兼容

**encoderCore**
- 文件：`include/utils/mpp/encoderCore.h`, `src/utils/mpp/encoderCore.cpp`
- 功能：MPP 编码核心
- 特点：Slot 池，状态机，双缓冲，编码线程

**jpegEncoder**
- 文件：`include/utils/mpp/jpegEncoder.h`, `src/utils/mpp/jpegEncoder.cpp`
- 功能：JPEG 编码器封装
- 特点：简化配置，文件保存

**streamWriter**
- 文件：`include/utils/mpp/streamWriter.h`, `src/utils/mpp/streamWriter.cpp`
- 功能：双线程分段写入器
- 特点：双写线程，分段切换，调度线程

**encoderPool**
- 文件：`include/utils/mpp/encoderPool.h`
- 功能：编码器池
- 特点：多核心，线程池

---

#### RGA 图像处理

**formatTool**
- 文件：`include/utils/rga/formatTool.h`
- 功能：RGA/DRM/V4L2 格式转换工具
- 特点：双向映射，注释详细

**rgaConverter**
- 文件：`include/utils/rga/rgaConverter.h`, `src/utils/rga/rgaConverter.cpp`
- 功能：RGA 转换器封装
- 特点：单例模式，多种操作，DMABUF 支持

**rgaProcessor**
- 文件：`include/utils/rga/rgaProcessor.h`, `src/utils/rga/rgaProcessor.cpp`
- 功能：RGA 处理线程
- 特点：线程池，缓冲池，双模式

---

#### 系统资源监控

**base**
- 文件：`include/utils/sys/base.h`
- 功能：资源监控基类
- 特点：模板方法，自动暂停，文件输出

**cpuMonitor**
- 文件：`include/utils/sys/cpuMonitor.h`, `src/utils/sys/cpuMonitor.cpp`
- 功能：CPU 使用率监控
- 特点：/proc/stat，差值计算

**memoryMonitor**
- 文件：`include/utils/sys/memoryMonitor.h`, `src/utils/sys/memoryMonitor.cpp`
- 功能：内存使用率监控
- 特点：/proc/meminfo，简化计算

---

#### V4L2 摄像头控制

**cameraController**
- 文件：`include/utils/v4l2/cameraController.h`, `src/utils/v4l2/cameraController.cpp`
- 功能：V4L2 摄像头控制器
- 特点：PImpl 惯用法，回调机制，双模式

**formatTool**
- 文件：`include/utils/v4l2/formatTool.h`
- 功能：V4L2 格式工具
- 特点：平面比例，格式映射

**frame**
- 文件：`include/utils/v4l2/frame.h`, `src/utils/v4l2/frame.cpp`
- 功能：统一帧接口
- 特点：双模式，内存池，元数据

**v4l2Exception**
- 文件：`include/utils/v4l2/v4l2Exception.h`
- 功能：V4L2 异常类
- 特点：错误日志

---

#### V4L2 参数控制

**paramControl**
- 文件：`include/utils/v4l2param/paramControl.h`, `src/utils/v4l2param/paramControl.cpp`
- 功能：V4L2 参数控制
- 特点：参数查询，参数对比，类型判断

**paramLogger**
- 文件：`include/utils/v4l2param/paramLogger.h`, `src/utils/v4l2param/paramLogger.cpp`
- 功能：参数变化日志
- 特点：静态方法

**paramProcessor**
- 文件：`include/utils/v4l2param/paramProcessor.h`, `src/utils/v4l2param/paramProcessor.cpp`
- 功能：参数处理器
- 特点：后台线程，回调机制，目标控制

---

## 🔍 快速查找

### 按功能查找

| 功能 | 模块 | 类/函数 |
|------|------|---------|
| 线程池 | Utils | asyncThreadPool |
| 内存池 | Utils | fixedSizePool, objectsPool |
| 并发队列 | Utils | concurrentqueue, safeQueue, orderedQueue |
| 日志记录 | Utils | logger |
| 设备监控 | Utils | udevMonitor, fdWrapper |
| DMA管理 | Utils | dmaBuffer |
| DRM显示 | Utils | deviceController, drmLayer, planesCompositor |
| MPP编码 | Utils | encoderCore, jpegEncoder |
| RGA处理 | Utils | rgaConverter, rgaProcessor |
| 摄像头控制 | Utils | cameraController |
| 系统监控 | Utils | cpuMonitor, memoryMonitor |
| 鼠标监控 | Utils | MouseWatcher |

### 按文件查找

| 文件 | 功能 | 关键类/函数 |
|------|------|-------------|
| `include/utils/asyncThreadPool.h` | 异步线程池 | ThreadPool |
| `include/utils/fixedSizePool.h` | 固定大小内存池 | FixedSizePool |
| `include/utils/dma/dmaBuffer.h` | DMA-BUF 管理 | DmaBuffer |
| `include/utils/drm/deviceController.h` | DRM设备管理 | DeviceController |
| `include/utils/mpp/encoderCore.h` | MPP 编码 | MppEncoderCore |
| `include/utils/rga/rgaProcessor.h` | RGA 处理 | RgaProcessor |
| `include/utils/v4l2/cameraController.h` | 摄像头控制 | CameraController |
| `include/utils/sys/cpuMonitor.h` | CPU监控 | CpuMonitor |

---

## 📚 外部依赖

### 系统库
- `libdrm` - DRM 库
- `libudev` - udev 库
- `pthread` - 线程库
- `epoll` - 事件监听
- `eventfd` - 事件通知

### 平台 SDK
- Rockchip MPP SDK - 媒体处理
- Rockchip RGA SDK - 2D 图形加速
- DRM - 显示输出
- V4L2 - 摄像头采集

### 第三方库
- [MoodyCamel ConcurrentQueue](https://github.com/cameron314/concurrentqueue) - 无锁队列  

---

## 📝 版本信息

- **项目版本**：1.0.0
- **库类型**：静态库 (libutils.a)
- **CMake 最低版本**：3.14
- **CMake 集成**：支持 add_subdirectory() 和 FetchContent
- **C++ 标准**：C++14
- **目标平台**：RK356x (ARMv8.2-A)

---

**最后更新**：2026-01-24
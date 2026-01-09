# EdgeVision 项目索引

> 本文档提供项目所有模块、类、关键函数的快速索引，便于代码检索和定位。

## 📑 目录

- [Model 模块](#model-模块)
- [Pipeline 模块](#pipeline-模块)
- [UI 模块](#ui-模块)
- [Utils 模块](#utils-模块)
- [示例代码](#示例代码)

---

## Model 模块

### 📁 路径
- 头文件：`include/model/`
- 源文件：`src/model/`

### 🏗️ 核心类

#### Yolov5s
**文件**：`include/model/yolov5s.h`, `src/model/yolov5s.cpp`

**功能**：YOLOv5 目标检测模型封装

**关键方法**：
- `init(rknn_app_context& inCtx, bool isChild)` - 初始化模型
- `infer(DmaBufferPtr in_dmabuf)` - 执行推理，返回检测结果
- `infer(DmaBufferPtr in_dmabuf, bool drawText)` - 执行推理并绘制检测框
- `setThresh(float BOX_THRESH, float NMS_THRESHresh)` - 设置置信度和 NMS 阈值

**依赖**：
- `rknnPool` - 推理线程池
- `preprocess` - 图像预处理
- `postprocess` - 模型后处理

---

#### rknnPool (模板类)
**文件**：`include/model/rknnPool.h`

**功能**：RKNN 推理线程池，支持多模型并行推理

**模板参数**：
- `rknnModel` - 模型类型（如 Yolov5s）
- `inputType` - 输入类型（如 DmaBufferPtr）
- `outputType` - 输出类型（如 object_detect_result_list）

**关键方法**：
- `init()` - 初始化线程池和模型
- `put(inputType inputData)` - 提交推理任务
- `get(outputType &outputData, int timeout)` - 获取推理结果
- `setThresh(float BOX_THRESH, float thNMS_THRESHresh)` - 设置阈值
- `clearFutures()` - 清空所有 future

**设计特点**：
- 使用 `asyncThreadPool` 实现任务调度
- 使用 `ConcurrentQueue` 管理 future 队列
- 支持模型上下文复制（`rknn_dup_context`）

---

### 🔧 工具函数

#### 预处理 (preprocess)
**文件**：`include/model/preprocess.h`, `src/model/preprocess.cpp`

**函数**：
- `convert_image_with_letterbox(const DmaBufferPtr& src, const DmaBufferPtr& dst, letterbox* letterbox, char color)` - Letterbox 预处理
- `convert_image_rga(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color)` - RGA 加速预处理
- `convert_image_cpu(const DmaBufferPtr& src, const DmaBufferPtr& dst, rect* src_box, rect* dst_box, char color)` - CPU 预处理

**依赖**：
- `RgaConverter` - RGA 图像转换

---

#### 后处理 (postprocess)
**文件**：`include/model/postprocess.h`, `src/model/postprocess.cpp`

**函数**：
- `read_class_names(const std::string& path, std::vector<std::string>& class_names)` - 读取类别名称
- `post_process_rule(rknn_app_context& app_ctx, rknn_tensor_mem* out_mem[], letterbox& lb, ...)` - YOLOv5 后处理（NMS）

**算法**：
- 快速 NMS（非极大值抑制）
- 支持量化和浮点模型
- 解码 YOLOv5 输出格式

---

#### 文件工具 (fileUtils)
**文件**：`include/model/fileUtils.h`, `src/model/fileUtils.cpp`

**函数**：
- `read_data_from_file(const char *path, char **out_data)` - 读取文件内容
- `readImage(const std::string& image_path, uint32_t format)` - 读取图像到 DMA-BUF
- `mapDmaBufferToMat(DmaBufferPtr img, bool copy)` - DMA-BUF 映射到 OpenCV Mat
- `saveImage(const std::string &image_path, DmaBufferPtr dma_buf)` - 保存 DMA-BUF 为图像
- `saveResultImage(DmaBufferPtr img, const object_detect_result_list& result_, ...)` - 保存带检测结果的图像

---

### 📊 数据结构

#### m_types.h
**文件**：`include/model/m_types.h`

**结构体**：
- `letterbox` - Letterbox 预处理参数（x_pad, y_pad, scale）
- `rect` - 矩形（left, top, right, bottom）
- `rect_pos` - 位置矩形（x, y, w, h）
- `object_detect_result` - 检测结果（box, prop, class_id, class_name）
- `Anchor` - Anchor 模板（w, h）
- `AnchorLayer` - Anchor 层（vector<Anchor>）
- `AnchorSet` - Anchor 集合（vector<AnchorLayer>）

**类型别名**：
- `object_detect_result_list` - 检测结果列表

---

#### yolov5.h
**文件**：`include/model/yolov5.h`, `src/model/yolov5.cpp`

**结构体**：
- `rknn_io_tensor_mem` - RKNN IO 内存（input_buf, input_mems, output_mems）
- `rknn_app_context` - RKNN 应用上下文（rknn_ctx, io_num, model_width/height, is_quant, input_attrs, output_attrs, io_mem）

**函数**：
- `loadModel(const char* model_path, rknn_app_context& app_ctx)` - 加载 RKNN 模型
- `loadIOnum(rknn_app_context& app_ctx)` - 加载输入输出信息
- `initializeMems(rknn_app_context& app_context)` - 初始化 IO 内存

---

## Pipeline 模块

### 📁 路径
- 头文件：`include/pipeline/`
- 源文件：`src/pipeline/`

### 🏗️ 核心类

#### AppController
**文件**：`include/pipeline/appController.h`, `src/pipeline/appController.cpp`

**功能**：应用主控制器，协调所有模块

**关键方法**：
- `start()` - 启动应用
- `quit()` - 退出应用

**内部实现（Impl）**：
- 管理 VisionPipeline、DisplayManager、UIRenderer、YoloProcessor
- 处理热插拔回调（preProcess、postProcess）
- 绑定 Qt 信号和槽

**依赖**：
- `VisionPipeline` - 视觉处理流水线
- `DisplayManager` - 显示管理器
- `UIRenderer` - UI 渲染器
- `YoloProcessor` - YOLO 推理处理器

---

#### VisionPipeline
**文件**：`include/pipeline/visionPipeline.h`, `src/pipeline/visionPipeline.cpp`

**功能**：视觉处理流水线，整合摄像头、RGA、MPP 等模块

**关键方法**：
- `start()` / `stop()` / `pause()` / `resume()` - 流水线控制
- `tryCapture()` - 拍照
- `tryRecord(RecordStatus status)` - 录像
- `setModelRunningStatus(ModelStatus status)` - 设置模型推理状态
- `registerOnRGA(RGACallBack cb_)` - 注册 RGA 回调
- `setMirrorMode(bool horizontal, bool vertical)` - 设置镜像模式
- `setExposurePercentage(float percentage)` - 设置曝光度
- `getCurrentRawFrame(FramePtr& frame)` - 获取原始帧
- `getCurrentRGAFrame(FramePtr& frame)` - 获取 RGA 处理后的帧
- `getFPS()` - 获取帧率
- `resetConfig(const CameraController::Config& newConfig)` - 重置配置

**内部实现（Impl）**：
- 双缓冲帧缓存（frameBuffer[2]）
- 独立录像线程
- RGA 处理线程
- FPS 统计（FpsPref）

**依赖**：
- `CameraController` - 摄像头控制
- `RgaProcessor` - RGA 处理
- `MppEncoderCore` - 视频编码
- `JpegEncoder` - JPEG 编码
- `StreamWriter` - 流写入
- `ParamControl` - 参数控制

---

#### DisplayManager
**文件**：`include/pipeline/displayManager.h`, `src/pipeline/displayManager.cpp`

**功能**：DRM 显示管理器，管理平面和图层

**关键方法**：
- `start()` / `stop()` - 启动/停止显示线程
- `registerPreRefreshCallback(RefreshCallback cb)` - 注册刷新前回调
- `registerPostRefreshCallback(RefreshCallback cb)` - 注册刷新后回调
- `presentFrame(PlaneHandle plane, std::vector<DmaBufferPtr> buffers, ...)` - 提交帧显示
- `createPlane(const PlaneConfig& config)` - 创建显示平面
- `getCurrentScreenSize()` - 获取当前屏幕尺寸

**内部实现（Impl）**：
- 主循环（mainLoop）处理帧提交
- PendingFrame 管理待显示帧
- Fence 同步（FenceWatcher）

**依赖**：
- `DrmDev` - DRM 设备
- `DrmLayer` - DRM 图层
- `PlanesCompositor` - 平面合成器
- `FenceWatcher` - Fence 监视

**数据结构**：
- `PlaneHandle` - 平面句柄（原子 ID）
- `PlaneConfig` - 平面配置（type, srcWidth, srcHeight, drmFormat, zOrder）
- `PlaneType` - 平面类型（OVERLAY, PRIMARY）

---

#### UIRenderer
**文件**：`include/pipeline/uiRenderer.h`, `src/pipeline/uiRenderer.cpp`

**功能**：Qt UI 渲染器，离屏渲染并合成到 DRM

**关键方法**：
- `init()` - 初始化 QWidget 和 QOpenGLContext
- `start()` / `stop()` / `pause()` / `resume()` - 渲染控制
- `resetTargetSize(const std::pair<uint32_t, uint32_t>& size)` - 重置目标尺寸
- `resetPlaneHandle(const DisplayManager::PlaneHandle& handle)` - 重置平面句柄
- `resetWidgetTargetRect(const DrawRect& targetRect)` - 重置 Widget 绘制区域
- `bindDisplayer(std::weak_ptr<DisplayManager> displayer)` - 绑定显示器
- `loadCursorIcon(const std::string& iconPath)` - 加载光标图标
- `drawText(...)` - 绘制文本
- `updateBoxs(object_detect_result_list&& ret)` - 更新检测框
- `setFPSUpdater(const fpsUpdater& cb)` - 设置 FPS 更新回调

**内部实现（Impl）**：
- 渲染定时器（renderTimer_）
- 资源监控定时器（resourceTimer_）
- DPI 缩放计算
- 鼠标监控（QMouseWatch）
- CPU/内存监控（CpuMonitor、MemoryMonitor）

**依赖**：
- `DisplayManager` - 显示管理器
- `Core` - OpenGL 上下文管理
- `Draw` - 绘制接口
- `MainInterface` - Qt 界面
- `QMouseWatch` - 鼠标监控

---

#### YoloProcessor
**文件**：`include/pipeline/yoloProcessor.h`, `src/pipeline/yoloProcessor.cpp`

**功能**：YOLO 推理处理器，管理推理线程池

**关键方法**：
- `start()` / `stop()` / `pause()` / `resume()` - 推理控制
- `setThresh(float BOX_THRESH, float thNMS_THRESHresh)` - 设置阈值
- `submit(DmaBufferPtr rgb, std::shared_ptr<void> holder)` - 提交推理任务
- `setOnResult(ResultCB cb)` - 设置结果回调

**内部实现（Impl）**：
- 使用 `rknnPool` 管理推理线程池
- 主循环（mainloop）获取推理结果
- holder 管理数据生命周期

**依赖**：
- `rknnPool` - 推理线程池
- `Yolov5s` - YOLO 模型

---

## UI 模块

### 📁 路径
- 头文件：`include/UI/`
- 源文件：`src/UI/`

### 🏗️ 核心类

#### MainInterface
**文件**：`include/UI/ConfigInterface/maininterface.h`, `src/UI/ConfigInterface/maininterface.cpp`

**功能**：Qt 主界面，提供参数配置和控制

**关键方法**：
- `updateFPS(const float fps)` - 更新 FPS 显示
- `updateCPUpayload(const float payload)` - 更新 CPU 负载
- `updateMemoryUsage(float usage)` - 更新内存使用
- `setUiDrawRect(const QRectF& r, qreal scale)` - 设置 UI 绘制区域

**信号**：
- `recordSignal(bool status)` - 录像信号
- `photoSignal()` - 拍照信号
- `confidenceChanged(float value)` - 置信度改变
- `exposureChanged(float value)` - 曝光度改变
- `captureModeChanged(CaptureMode mode)` - 捕获模式改变
- `mirrorModeChanged(MirrorMode mode)` - 镜像模式改变
- `modelModeChange(ModelMode mode)` - 模型开启状态

**枚举**：
- `CaptureMode` - 捕获模式（Video, Photo）
- `MirrorMode` - 镜像模式（Normal, Horizontal, Vertical, Both）
- `ModelMode` - 模型模式（Run, Stop）

**特性**：
- 防抖机制（debounceSlider）
- DPI 缩放（computeDPIScale）
- 自定义鼠标事件（event）

**依赖**：
- `QMouseWatch` - 鼠标监控

---

#### Core
**文件**：`include/UI/rander/core.h`, `src/UI/rander/core.cpp`

**功能**：OpenGL 上下文管理，DMABUF 到 EGLImage 的导入

**关键方法**：
- `instance()` - 获取单例
- `shutdown()` - 关闭上下文
- `queryAllFormats(uint32_t targetFmt)` - 查询支持的格式
- `registerResSlot(const std::string& type, size_t poolSize, ...)` - 注册资源槽
- `acquireFreeSlot(const std::string &type, int timeout_ms)` - 获取空闲槽
- `releaseSlot(const std::string& type, std::shared_ptr<resourceSlot>& slot)` - 释放槽
- `makeQCurrent()` - 绑定 Qt 上下文
- `doneQCurrent()` - 解绑上下文

**数据结构**：
- `resourceSlot` - 资源槽（dmabufPtr, eglImage, textureId, blitFbo, qfbo）
  - `getSyncFence(int& fence)` - 同步到 DMA-BUF

**设计特点**：
- 单例模式
- 使用 Qt OpenGL 上下文
- 支持 DMABUF 导入（EGLImage）
- 多缓冲循环使用

**依赖**：
- EGL - EGLImage 创建
- Qt OpenGL - QOpenGLContext, QOffscreenSurface
- DmaBuffer - DMA-BUF 管理

---

#### Draw
**文件**：`include/UI/rander/draw.h`, `src/UI/rander/draw.cpp`

**功能**：绘制接口，封装 QPainter 操作

**关键方法**：
- `clear(QOpenGLFramebufferObject* fbo, const QColor& color)` - 清空画布
- `drawText(...)` - 绘制文本
- `drawImage(...)` - 绘制图像
- `drawBoxes(...)` - 绘制检测框
- `drawWidget(...)` - 绘制 Widget

**渲染模式**：
- `KeepAspectRatio` - 保持宽高比
- `StretchToFill` - 拉伸填充
- `CenterNoScale` - 居中不缩放

**依赖**：
- `Core` - OpenGL 上下文
- QPainter - Qt 绘制

---

#### QMouseWatch
**文件**：`include/UI/qMouseWatch.h`

**功能**：Qt 鼠标事件监听器，继承自 MouseWatcher

**关键方法**：
- `setNotifyWindow(QWidget* win)` - 设置通知窗口

**特性**：
- 自定义鼠标事件（CustomMouseEvent）
- 异步事件分发（QMetaObject::invokeMethod）

**依赖**：
- `MouseWatcher` - 底层鼠标监控

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

## 示例代码

### 📁 路径
- `examples/`

### 📝 示例列表

#### app.cpp
**功能**：完整应用示例
**依赖**：AppController, Qt
**特点**：信号处理，自动退出

#### visionTest.cpp
**功能**：独立视频流显示测试
**依赖**：VisionPipeline, DisplayManager
**特点**：热插拔回调，分辨率自适应

#### pipelineTest.cpp
**功能**：流水线功能测试
**依赖**：VisionPipeline
**特点**：录像/拍照测试，循环测试

#### SnowflakeTest.cpp
**功能**：DRM 雪花屏测试
**依赖**：DisplayManager, RGA
**特点**：随机噪声填充，FPS 统计

#### UITest.cpp
**功能**：独立 UI 渲染测试
**依赖**：DisplayManager, UIRenderer, Qt
**特点**：Qt 主循环，鼠标交互

---

## 🔍 快速查找

### 按功能查找

| 功能 | 模块 | 类/函数 |
|------|------|---------|
| 摄像头采集 | Utils | CameraController |
| 图像格式转换 | Utils | RgaProcessor |
| 模型推理 | Model | Yolov5s, rknnPool |
| 视频编码 | Utils | MppEncoderCore, JpegEncoder |
| 显示输出 | Pipeline | DisplayManager |
| UI 渲染 | UI | UIRenderer, Core, Draw |
| 线程管理 | Utils | asyncThreadPool, ThreadUtils |
| 内存管理 | Utils | fixedSizePool, objectsPool |
| 设备监控 | Utils | udevMonitor, MouseWatcher |

### 按文件查找

| 文件 | 功能 | 关键类/函数 |
|------|------|-------------|
| `include/model/yolov5s.h` | YOLO 模型 | Yolov5s |
| `include/pipeline/visionPipeline.h` | 视觉流水线 | VisionPipeline |
| `include/pipeline/displayManager.h` | 显示管理 | DisplayManager |
| `include/UI/rander/core.h` | OpenGL 上下文 | Core |
| `include/utils/dma/dmaBuffer.h` | DMA-BUF 管理 | DmaBuffer |
| `include/utils/mpp/encoderCore.h` | MPP 编码 | MppEncoderCore |
| `include/utils/rga/rgaProcessor.h` | RGA 处理 | RgaProcessor |
| `include/utils/v4l2/cameraController.h` | 摄像头控制 | CameraController |

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
- RKNN-Toolkit - 模型推理

### 第三方库
- Qt5 - GUI 框架
- OpenCV - 图像处理
- [MoodyCamel ConcurrentQueue](https://github.com/cameron314/concurrentqueue) - 无锁队列  

---

## 📝 版本信息

- **项目版本**：1.0
- **CMake 最低版本**：3.14
- **C++ 标准**：C++14
- **目标平台**：RK356x (ARMv8.2-A)

---

**最后更新**：2026-01-01
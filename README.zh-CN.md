# 🎯 utilsCore
<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-CN.md">简体中文</a>
</p>


> 面向嵌入式视觉系统的数据链路与硬件抽象工具集

## 📖 项目简介

utilsCore 是一个面向嵌入式平台的 C++ 工具库,  
主要用于构建和验证 **高性能、多媒体数据通路** 相关的工程方案.

项目最初源自 EdgeVision 系统在真实硬件环境中的工程实践,  
随着功能逐渐稳定, 被拆分为独立仓库, 以沉淀可复用的基础能力.

当前版本主要针对 Rockchip RK356x 平台进行设计和验证,  
但整体架构并不强依赖具体 SoC, 更关注 **数据流组织方式、硬件抽象边界与资源管理策略**.

---

## 🎯 项目定位与目标

utilsCore **不是一个通用意义上的“基础库”**,  
也不以“开箱即用”或“高度稳定”作为当前阶段目标.

它更关注以下几个方向:

- 在真实嵌入式硬件上验证 **低延迟数据链路设计**
- 探索 DMA-BUF 为核心的 **零拷贝跨模块数据传递**
- 提供对常见多媒体硬件模块的 **工程级封装**
- 支撑上层应用进行性能测试、系统实验与架构演进

是否将其作为长期基础层,  
应由具体项目的需求、约束与维护能力自行判断.

---

## 🧠 设计理念

- **数据链路优先**
  
  项目关注的是 *数据如何流动*,  
  而非单一功能模块本身.

- **硬件能力显式化**
  
  不刻意隐藏硬件特性,  
  而是将其作为设计前提, 主动暴露约束与能力边界.

- **工程可读性优于抽象完整性**
  
  相比完美抽象, 更重视:
  - 生命周期是否清晰
  - 资源是否可控
  - 行为是否可推导

- **服务于真实负载**
  
  模型推理、UI 绘制等模块并非功能目标,  
  而是用于构造接近真实应用的系统负载.

---

## 🏗️ 系统能力概览

utilsCore 当前主要覆盖以下能力模块:

- **V4L2 摄像头采集**
  - 多平面支持
  - DMABUF 输出
  - 参数控制与日志

- **DMA-BUF 资源管理**
  - 引用计数
  - 生命周期约束
  - 跨模块安全传递

- **RGA 图像处理**
  - 格式转换
  - 分辨率缩放
  - 硬件加速路径验证

- **MPP 编码**
  - JPEG 图像编码
  - H.264 视频编码
  - 流式写入支持

- **DRM 显示**
  - 多 Plane 管理
  - Overlay / Primary 组合
  - 热插拔监控

- **基础并发与系统工具**
  - 线程池
  - 队列与对象池
  - 资源监控与辅助工具

---

## 🧩 使用方式预期

utilsCore 更适合以下使用场景:

- 嵌入式视觉系统的原型验证
- 多媒体数据通路的性能测试
- 硬件加速路径的工程探索
- 作为上层应用或系统的能力子集

它假设使用者:
- 具备一定嵌入式或系统开发经验
- 理解底层硬件与 Linux 多媒体栈
- 愿意为具体场景进行裁剪或修改

---

## 📌 非目标（Non-goals）

在当前阶段, utilsCore **不尝试解决** 以下问题:

- 通用跨平台适配
- 完整的错误恢复与异常兜底
- 长期 ABI / API 稳定性承诺
- 面向初学者的教学封装

这些能力如果未来需要,  
应在明确需求背景下逐步演进.

---

## 🚀 快速开始

### 环境要求

- **目标平台**：RK356x (ARMv8.2-A)
- **编译环境**：Ubuntu 20.04+ (x86_64)
- **工具链**：GCC 9.0+ (支持 C++14)
- **依赖库**：
  - Rockchip MPP SDK
  - Rockchip RGA SDK
  - libdrm
  - libudev
  - pthread
  - epoll

### 方式一：作为静态库使用

#### 独立构建库文件

```bash
# 1. 克隆项目
git clone https://github.com/SweerItTer/utilsCore.git

# 2. 配置交叉编译器路径
export TOOLCHAIN_PATH=YOUR_TOOLCHAIN_PATH # e.g:~/rk3568/buildroot/output/rockchip_rk3568/host

# 3. 配置并构建
mkdir -p build_utilsCore && cd build_utilsCore
mkdir -p build_utilsCore && cd build_utilsCore

cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
    -DCMAKE_TOOLCHAIN_FILE=../utilsCore/rk356x-toolchain.cmake \
    -DTOOLCHAIN_PATH=$TOOLCHAIN_PATH \
    -DUSE_CROSS_COMPILE=ON \
    -DBUILD_STATIC_UTILS=ON \
    --no-warn-unused-cli -S ../utilsCore -B .

cmake --build . --config Release --target utils utils_static -j$(nproc) 

# 输出：build_utilsCore/src/utils/libutils.a
```
##### 检查结果
```
file src/utils/libutils.a
ar t src/utils/libutils.a

## 输出:

./libutils.a: current ar archive
asyncThreadPool.cpp.o
dmaBuffer.cpp.o
deviceController.cpp.o
drmLayer.cpp.o
planesCompositor.cpp.o
logger.cpp.o
watcher.cpp.o
encoderContext.cpp.o
encoderCore.cpp.o
jpegEncoder.cpp.o
streamWriter.cpp.o
rgaConverter.cpp.o
rgaProcessor.cpp.o
threadPauser.cpp.o
udevMonitor.cpp.o
cameraController.cpp.o
frame.cpp.o
paramControl.cpp.o
paramLogger.cpp.o
paramProcessor.cpp.o
```

#### 集成到项目

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApp)

# 添加头文件路径
include_directories(/path/to/utilsCore/include)

# 链接静态库
link_directories(/path/to/utilsCore/build/src/utils)

add_executable(my_app src/main.cpp)

# 链接库（需要显式添加 pthread）
target_link_libraries(my_app utils pthread)
```

### 方式二：作为 Git 子模块（推荐）

这是最简单、最灵活的方式。源文件就像项目原生文件一样，编译器自动处理依赖。

```bash
# 添加子模块
git submodule add https://github.com/SweerItTer/utilsCore.git third_party/utilsCore
git submodule update --init --recursive
```

**CMakeLists.txt**:
```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApp)

# 添加 utilsCore 子目录
add_subdirectory(third_party/utilsCore)

# 创建可执行文件
add_executable(my_app 
    src/main.cpp
    # 编译器会自动处理依赖
    # 只用到的文件才会被编译
)

# 链接库（需要显式添加 pthread）
target_link_libraries(my_app utils pthread)
```

**优点**：
- ✅ 版本锁定，离线可用
- ✅ 编译器自动处理依赖，只用到的文件才会编译
- ✅ 无需手动选择模块
- ✅ 调试时可以单步进入库代码

### 使用示例

```cpp
#include <utils/v4l2/cameraController.h>

int main() {
    CameraController::Config cfg{};
    // 使用V4L2摄像头
    CameraController camera(cfg);
    setFrameCallback([](FramePtr f){
        printf("Frame Size:%dx%d\n", f->meta.w, f->meta.h);
    });
    camera.start();
    sleep(10);
    camera.stop();
    return 0;
}
```

**编译器行为**：
- 只编译 `cameraController.cpp` 及其依赖
- 不会编译其他未使用的源文件（如 `rgaProcessor.cpp` 等）
- 链接时只包含用到的符号，二进制体积自动优化

## 📚 技术栈

### 核心依赖

| 依赖库 | 版本 | 用途 |
|--------|------|------|
| Rockchip MPP SDK | - | 视频编解码 |
| Rockchip RGA SDK | - | 2D 图像加速 |
| DRM | - | 显示输出 |
| V4L2 | - | 摄像头采集 |

## 📖 文档

- **API 相关**：详见 [API Wiki](./api_wiki.md)
- **示例代码**：本仓库将不提供示例
> 但是可在 [EdgeVision-app](https://github.com/SweerItTer/EdgeVision-app) 查看 `examples/` 和 `src/pipeline` 目录

## 🤝 贡献指南

欢迎提交 Issue 和 Pull Request！

## 📄 许可证

Apache License 2.0 | 详见 [LICENSE](LICENSE)

## 👨‍💻 作者

[SweerItTer](https://github.com/SweerItTer)

[xxxzhou.xian@foxmail.com](mailto:xxxzhou.xian@foxmail.com)

## 🙏 致谢

感谢 Rockchip 提供的硬件加速 SDK 和开源社区的支持。

---

**注意**：本项目从专注于传输链路的设计与实现的项目独立而来，暂不包含所有附加功能的鲁棒性保证。
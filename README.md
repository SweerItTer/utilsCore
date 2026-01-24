# 🎯 utilsCore
<p align="center">
  <a href="README.md">English</a> |
  <a href="README.zh-CN.md">简体中文</a>
</p>

> Data-path–oriented hardware abstraction and multimedia utilities for embedded vision systems

## 📖 Overview

utilsCore is a C++ utility library for embedded platforms,  
primarily designed to build and evaluate **high-performance multimedia data paths**.

The project originates from real-world engineering practice in the EdgeVision system.  
As the underlying components gradually stabilized, they were extracted into an independent repository to preserve reusable, low-level capabilities.

The current implementation is mainly designed and validated on Rockchip RK356x platforms.  
However, the architecture is **not tightly coupled to a specific SoC**.  
Instead, it focuses on **data-flow organization, hardware abstraction boundaries, and resource management strategies**.

---

## 🎯 Project Positioning

utilsCore is **not a general-purpose foundational library**,  
nor does it aim to be “plug-and-play” or fully production-hardened at this stage.

Its primary goals are:

- To validate **low-latency data-path designs** on real embedded hardware
- To explore **DMA-BUF–centric zero-copy data transfer** across modules
- To provide **engineering-level wrappers** around common multimedia hardware blocks
- To support performance testing, system experiments, and architectural iteration in upper-layer applications

Whether it should serve as a long-term base layer  
depends entirely on the requirements, constraints, and maintenance capacity of a given project.

---

## 🧠 Design Principles

- **Data-path first**

  The project focuses on *how data flows through the system*,  
  rather than isolated functionality of individual modules.

- **Explicit hardware exposure**

  Hardware characteristics are not intentionally hidden.  
  Constraints and capabilities are treated as design premises and exposed explicitly.

- **Engineering clarity over abstraction completeness**

  Instead of pursuing perfect abstraction, the project prioritizes:
  - Clear object lifetimes
  - Controllable resource ownership
  - Predictable and traceable behavior

- **Designed for real workloads**

  Components such as model inference or UI rendering are not core goals by themselves.  
  They exist to introduce realistic system load for evaluating data paths and resource contention.

---

## 🏗️ Capability Overview

utilsCore currently covers the following functional areas  
(each module can be used independently and is not a hard dependency of others):

- **V4L2 Camera Capture**
  - Multi-plane support
  - DMA-BUF output
  - Parameter control and logging

- **DMA-BUF Resource Management**
  - Reference counting
  - Lifetime constraints
  - Safe cross-module transfer

- **RGA Image Processing**
  - Format conversion
  - Resolution scaling
  - Hardware-accelerated path validation

- **MPP Encoding**
  - JPEG image encoding
  - H.264 video encoding
  - Streaming-oriented writers

- **DRM Display**
  - Multi-plane management
  - Overlay / Primary composition
  - Hotplug monitoring

- **Concurrency and System Utilities**
  - Thread pools
  - Queues and object pools
  - Resource monitoring helpers

---

## 🧩 Intended Usage

utilsCore is best suited for:

- Prototyping embedded vision systems
- Evaluating multimedia data-path performance
- Exploring hardware-accelerated pipelines
- Serving as a capability subset for higher-level applications or services

It assumes that users:

- Have prior experience with embedded or systems programming
- Understand Linux multimedia subsystems
- Are willing to modify or trim the codebase for specific use cases

---

## 📌 Non-goals

At its current stage, utilsCore **does not attempt to provide**:

- General-purpose cross-platform support
- Comprehensive fault recovery or error masking
- Long-term ABI or API stability guarantees
- Beginner-oriented educational abstractions

If such capabilities are required,  
they should be introduced incrementally under clearly defined requirements.

---

## 🚀 Quick Start

### Environment Requirements

- **Target Platform**: RK356x (ARMv8.2-A)
- **Host System**: Ubuntu 20.04+ (x86_64)
- **Toolchain**: GCC 9.0+ (C++14 required)
- **Dependencies**:
  - Rockchip MPP SDK
  - Rockchip RGA SDK
  - libdrm
  - libudev
  - pthread
  - epoll

---

### Option 1: Build as a Static Library

#### Build utilsCore Independently

```bash
# 1. Clone repository
git clone https://github.com/SweerItTer/utilsCore.git

# 2. Configure toolchain path
export TOOLCHAIN_PATH=YOUR_TOOLCHAIN_PATH
# e.g. ~/rk3568/buildroot/output/rockchip_rk3568/host

# 3. Configure and build
mkdir -p build_utilsCore && cd build_utilsCore

cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
    -DCMAKE_TOOLCHAIN_FILE=../utilsCore/rk356x-toolchain.cmake \
    -DTOOLCHAIN_PATH=$TOOLCHAIN_PATH \
    -DUSE_CROSS_COMPILE=ON \
    -DBUILD_STATIC_UTILS=ON \
    --no-warn-unused-cli -S ../utilsCore -B .

cmake --build . --config Release --target utils utils_static -j$(nproc) 

# Output: build_utilsCore/src/utils/libutils.a
```

##### check

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

#### Integrate into Your Project

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApp)

include_directories(/path/to/utilsCore/include)
link_directories(/path/to/utilsCore/build/src/utils)

add_executable(my_app src/main.cpp)

# pthread must be explicitly linked
target_link_libraries(my_app utils pthread)
```

------

### Option 2: Use as a Git Submodule (Recommended)

This is the most flexible approach.
utilsCore behaves like native project code, and only required modules are compiled.

```bash
git submodule add https://github.com/SweerItTer/utilsCore.git third_party/utilsCore
git submodule update --init --recursive
```

**CMakeLists.txt**:

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyApp)

add_subdirectory(third_party/utilsCore)

add_executable(my_app
    src/main.cpp
)

target_link_libraries(my_app utils pthread)
```

**Advantages**:

- Version locked, reproducible builds
- Only referenced source files are compiled
- No manual module selection
- Full debug visibility into utilsCore internals

------

### Minimal Usage Example

```cpp
#include <utils/v4l2/cameraController.h>

int main() {
    CameraController::Config cfg{};

    CameraController camera(cfg);

    setFrameCallback([](FramePtr f) {
        printf("Frame Size: %dx%d\n", f->meta.w, f->meta.h);
    });

    camera.start();
    sleep(10);
    camera.stop();
    return 0;
}
```

**Compiler Behavior**:

- Only `cameraController.cpp` and its dependencies are compiled
- Unused modules (e.g. RGA processing) are excluded
- Final binary contains only referenced symbols

------

## 📚 Technology Stack

### Core Dependencies

| Dependency       | Purpose               |
| ---------------- | --------------------- |
| Rockchip MPP SDK | Video encode/decode   |
| Rockchip RGA SDK | 2D image acceleration |
| DRM              | Display output        |
| V4L2             | Camera capture        |

------

## 📖 Documentation

- **API Reference**: see [API Wiki](./api_wiki.md)
- **Examples**: not included in this repository
  → Refer to [EdgeVision-app](https://github.com/SweerItTer/EdgeVision-app)
  (`examples/` and `src/pipeline` directories)

------

## 🤝 Contributing

Issues and Pull Requests are welcome.
Bug reports, especially hardware-specific behavior, are highly appreciated.

------

## 📄 License

Apache License 2.0
See [LICENSE](LICENSE)

------

## 👨‍💻 Author

[SweerItTer](https://github.com/SweerItTer)
[xxxzhou.xian@foxmail.com](mailto:xxxzhou.xian@foxmail.com)

------

## 🙏 Acknowledgements

Thanks to Rockchip for providing hardware acceleration SDKs and to the open-source community.

------

**Note**:
This project was extracted from a system originally focused on data path and pipeline design.
Not all auxiliary features are guaranteed to be production-hardened.

# 高频回调/遍历优化清单

| 优化优先级 | 文件路径(行号)-操作 |
|---|---|
| P0 | `src/utils/v4l2/cameraController.cpp(763)` - `captureLoop` 主循环（每帧执行） |
| P0 | `src/utils/v4l2/cameraController.cpp(794)` - 判断并触发 `enqueueCallback_` |
| P0 | `src/utils/v4l2/cameraController.cpp(795)` - `enqueueCallback_(std::move(frame_opt))` 回调调用 |
| P0 | `include/utils/v4l2/cameraController.h(26)` - `FrameCallback` 使用 `std::function<void(FramePtr)>` |
| P0 | `src/utils/v4l2/cameraController.cpp(752)` - 每帧 `setReleaseCallback` 设置 lambda |
| P0 | `include/utils/v4l2/frame.h(77)` - `Frame` 内保存 `std::function<void(int)>` |
| P0 | `src/utils/v4l2/frame.cpp(19)` - 析构时调用释放回调 `bufReleasCallback_` |
| P0 | `include/utils/asyncThreadPool.h(136)` - 任务队列元素类型 `ConcurrentQueue<std::function<void()>>` |
| P0 | `src/utils/asyncThreadPool.cpp(46)` - worker 循环（高频） |
| P0 | `src/utils/asyncThreadPool.cpp(62)` - 执行 `task()`（类型擦除调用） |
| P0 | `src/utils/rga/rgaProcessor.cpp(270)` - `run` 主循环（持续调度） |
| P0 | `src/utils/rga/rgaProcessor.cpp(274)` - `try_enqueue([this](){ return infer(); })` |
| P0 | `src/utils/rga/rgaProcessor.cpp(258)` - `setReleaseCallback` 设置 lambda |
| P1 | `src/utils/mouse/watcher.cpp(190)` - 鼠标事件主循环 |
| P1 | `src/utils/mouse/watcher.cpp(222)` - 遍历 handlers 并判定 predicate |
| P1 | `src/utils/mouse/watcher.cpp(224)` - 每事件创建线程并调用 `cb` |
| P1 | `src/utils/mouse/watcher.cpp(379)` - 位置回调触发入口 |
| P1 | `src/utils/mouse/watcher.cpp(390)` - 遍历原始坐标回调并触发 |
| P1 | `src/utils/mouse/watcher.cpp(399)` - 遍历映射坐标回调并触发 |
| P1 | `include/utils/mouse/watcher.h(31)` - `EventCallback` 为 `std::function` |
| P1 | `include/utils/mouse/watcher.h(32)` - `PositionCallback` 为 `std::function` |
| P2 | `src/utils/udevMonitor.cpp(284)` - 遍历 `snapshot` handlers |
| P2 | `src/utils/udevMonitor.cpp(286)` - predicate 调用 `h.pred(subs, action)` |
| P2 | `src/utils/udevMonitor.cpp(290)` - `asyncPool.enqueue(h.cb)`（回调包装入池） |
| P2 | `include/utils/udevMonitor.h(31)` - `Callback` 为 `std::function<void()>` |
| P2 | `include/utils/udevMonitor.h(64)` - `pred` 为 `std::function<bool(...)>` |
| P2 | `src/utils/drm/deviceController.cpp(137)` - 遍历 pre-refresh 回调并调用 |
| P2 | `src/utils/drm/deviceController.cpp(146)` - 遍历 post-refresh 回调并调用 |
| P2 | `include/utils/drm/deviceController.h(123)` - `ResourceCallback` 为 `std::function<void()>` |
| P2 | `include/utils/fenceWatcher.h(22)` - `watchFence` 接口接收 `std::function<void()>` |
| P2 | `include/utils/fenceWatcher.h(99)` - 触发 fence 回调 `data.callback()` |
| P3 | `include/utils/drm/drmLayer.h(111)` - 属性 setter 映射 `unordered_map<string, std::function<...>>` |
| P3 | `include/utils/drm/drmLayer.h(112)` - 属性 getter 映射 `unordered_map<string, std::function<...>>` |
| P3 | `include/utils/drm/drmLayer.h(72)` - `setProperty` 查表并调用 setter |
| P3 | `include/utils/drm/drmLayer.h(84)` - `getProperty` 查表并调用 getter |

# 无继承/无类型擦除的可复用小库方案（编译期多态）

下面给一个 header-only 方案：  
- 不使用继承；  
- 不使用 `std::function` / 虚函数；  
- 回调与帧生产器均为模板参数，调用点可被内联。

```cpp
// include/utils/fast_callback_camera.h
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fastcam {

class Frame {
public:
    Frame() {
        data_.reserve(10);
        data_.emplace_back('n');
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&&) noexcept = default;
    Frame& operator=(Frame&&) noexcept = default;
    ~Frame() = default;

    const std::vector<char>& get() const noexcept { return data_; }

private:
    std::vector<char> data_;
};

using FramePtr = std::shared_ptr<Frame>;

struct DefaultProducer {
    FramePtr operator()() const { return std::make_shared<Frame>(); }
};

template <typename Callback, typename Producer = DefaultProducer>
class CameraLoop final {
public:
    CameraLoop(Callback callback, Producer producer = Producer{})
        : callback_(std::move(callback)), producer_(std::move(producer)) {}

    ~CameraLoop() { stop(); }
    CameraLoop(const CameraLoop&) = delete;
    CameraLoop& operator=(const CameraLoop&) = delete;
    CameraLoop(CameraLoop&&) = delete;
    CameraLoop& operator=(CameraLoop&&) = delete;

    void start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&CameraLoop::run, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() {
        uint32_t counter = 0;
        while (running_.load(std::memory_order_relaxed)) {
            if (++counter == 100) {
                counter = 0;
                callback_(producer_());  // 编译期确定调用目标
            }
        }
    }

    std::atomic<bool> running_{false};
    std::thread worker_;
    Callback callback_;
    Producer producer_;
};

template <typename Callback, typename Producer = DefaultProducer>
auto make_camera_loop(Callback&& cb, Producer&& producer = Producer{})
-> CameraLoop<typename std::decay<Callback>::type, typename std::decay<Producer>::type> {
    using Cb = typename std::decay<Callback>::type;
    using Pd = typename std::decay<Producer>::type;
    return CameraLoop<Cb, Pd>(std::forward<Callback>(cb), std::forward<Producer>(producer));
}

} // namespace fastcam
```

### 最小复现用法

```cpp
#include <iostream>
#include "utils/fast_callback_camera.h"

int main() {
    auto cb = [](fastcam::FramePtr frame) {
        std::cout << frame->get()[0] << '\n';
    };

    auto camera = fastcam::make_camera_loop(cb);
    camera.start();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    camera.stop();
    return 0;
}
```

### 在当前工程中的替换映射（首批）

- `CameraController::FrameCallback`（`include/utils/v4l2/cameraController.h(26)`）可改为模板参数化回调持有器，避免 `std::function`。
- `Frame::bufReleasCallback_`（`include/utils/v4l2/frame.h(77)`）可改为“回收器策略对象 + index”，把释放路径也改成编译期分发。
- `asyncThreadPool` 的任务体（`include/utils/asyncThreadPool.h(136)`）可进一步拆成“固定任务类型队列”（针对热点路径），避免通用 `std::function<void()>`。

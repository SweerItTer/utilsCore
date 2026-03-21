# Camera Capture Performance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a self-contained camera capture performance example under `examples/camera_perf/` that records per-frame latency metrics and generates paper-ready comparison figures.

**Architecture:** Add minimal timestamp hooks to CameraController metadata, build a dedicated benchmark executable, and pair it with a Python plotting script that converts CSV outputs into mixed frame-and-data figure boards. Keep all performance-test assets in one example subdirectory for clarity.

**Tech Stack:** C++14, CameraController, V4L2, CMake, Python 3, CSV, matplotlib, adb-friendly runtime workflow

---

### Task 1: Add raw timestamps needed by the benchmark

**Files:**
- Modify: `include/utils/v4l2/frame.h`
- Modify: `src/utils/v4l2/frame.cpp`
- Modify: `src/utils/v4l2/cameraController.cpp`

**Step 1: Add a failing compile use-site expectation**

Reference expectation for the benchmark:

```cpp
const auto dq_us = frame->meta.dequeue_us;
const auto cb_us = frame->meta.callback_us;
```

Expected: compile fails because these fields do not exist yet.

**Step 2: Add minimal metadata fields**

Extend frame metadata with:

- `dequeue_us`
- `callback_us`

Populate them in CameraController:

- timestamp immediately after successful `VIDIOC_DQBUF`
- timestamp immediately before user callback invocation

**Step 3: Verify library still builds**

Run: project build command for `utils`

**Step 4: Commit**

```bash
git add include/utils/v4l2/frame.h src/utils/v4l2/frame.cpp src/utils/v4l2/cameraController.cpp
git commit -m "update: 为采集性能测试补充原始时间戳 metadata"
```

### Task 2: Add the camera performance example

**Files:**
- Create: `examples/camera_perf/camera_capture_perf.cpp`
- Modify: `examples/CMakeLists.txt`

**Step 1: Write the failing target expectation**

Expected new command:

```bash
cmake --build <build-dir> --target Camera_Capture_Perf
```

Expected: target missing before implementation.

**Step 2: Implement the executable**

The example should support:

- `--device`
- `--width`
- `--height`
- `--format`
- `--frames`
- `--buffer-count`
- `--use-dmabuf`
- `--output-dir`

The runner should:

- start CameraController
- record per-frame metrics
- compute summary statistics
- dump CSV and summary text

**Step 3: Verify target builds**

Run the build for `Camera_Capture_Perf`.

**Step 4: Commit**

```bash
git add examples/CMakeLists.txt examples/camera_perf/camera_capture_perf.cpp
git commit -m "add: 新增 CameraController 帧间延时性能测试例程"
```

### Task 3: Add plotting and figure-generation scripts

**Files:**
- Create: `examples/camera_perf/plot_camera_perf.py`
- Create: `examples/camera_perf/README.md`

**Step 1: Write the failing script contract**

Expected usage:

```bash
python3 examples/camera_perf/plot_camera_perf.py --inputs run_720.csv run_1080.csv --output-dir out
```

Expected: script missing before implementation.

**Step 2: Implement plotting**

Generate:

- time-series plot
- distribution plot
- summary table export
- mixed board with sampled frames plus plots

**Step 3: Verify script help and basic parse**

Run:

```bash
python3 examples/camera_perf/plot_camera_perf.py --help
```

**Step 4: Commit**

```bash
git add examples/camera_perf/plot_camera_perf.py examples/camera_perf/README.md
git commit -m "add: 新增采集性能论文配图脚本和使用说明"
```

### Task 4: Verify end-to-end build wiring

**Files:**
- Modify if needed: `examples/CMakeLists.txt`

**Step 1: Rebuild the example with the full project command**

Run:

```bash
/usr/bin/cmake -DCMAKE_BUILD_TYPE:STRING=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE -DCMAKE_TOOLCHAIN_FILE=/home/mouj/Projects/EdgeVision/EdgeVision-app/rk356x-toolchain.cmake -DTOOLCHAIN_PATH=/home/mouj/rk3568/buildroot/output/rockchip_rk3568/host -DUSE_CROSS_COMPILE=ON --no-warn-unused-cli -S /home/mouj/Projects/EdgeVision/EdgeVision-app -B /home/mouj/Projects/EdgeVision/build
/usr/bin/cmake --build /home/mouj/Projects/EdgeVision/build --config Debug --target Camera_Capture_Perf -j 20 --
```

**Step 2: Verify clean output**

Expected: target builds successfully with no merge leftovers.

**Step 3: Commit**

```bash
git add examples/CMakeLists.txt
git commit -m "update: 完成采集性能测试例程的构建接入"
```

### Task 5: Prepare paper-facing run recipe

**Files:**
- Modify: `examples/camera_perf/README.md`

**Step 1: Document recommended run matrix**

Include:

- `640x480`
- `1280x720`
- `1920x1080`
- suggested `300~1000` frames each

**Step 2: Document figure generation**

Include exact commands for:

- local run
- adb deployment
- pulling CSV and sampled frames
- generating the final board figure

**Step 3: Commit**

```bash
git add examples/camera_perf/README.md
git commit -m "docs: 补充采集性能实验与论文出图流程"
```

# Camera Capture Performance Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a camera capture performance example and plotting workflow that can support a paper-style Figure 3-6 using the current `fix-rga-stop` codebase.

**Architecture:** Keep the performance work self-contained under `examples/camera_perf/` so the C++ runner, CSV outputs, plotting scripts, and usage notes live together. Measure both frame-to-frame callback interval and CameraController software-path overhead, then generate mixed figure boards combining real sampled frames with latency distributions and summary tables.

**Tech Stack:** C++14, CameraController, V4L2, DMABUF/MMAP, CMake, Python 3, CSV, matplotlib

---

## Design Summary

The old `performance` tag is no longer used as an implementation baseline. The `perf/camera-test` branch is rebased onto the latest `fix-rga-stop` state so the performance example is written against the current working CameraController implementation and current build system.

The example will not try to prove "zero overhead" from a single average latency figure. Instead it will emit enough raw data to support a mixed paper figure:

- Real frame mosaics for each tested resolution
- Per-frame interval time-series plots
- Distribution plots for frame interval and software overhead
- A compact summary table with `mean / median / std / p95 / p99 / min / max / dropped`

## Metrics

Two metrics are collected per delivered frame:

- `frame_interval_us`
  Defined as the callback timestamp of frame N minus the callback timestamp of frame N-1. This shows whether the software path disturbs the camera cadence.
- `controller_overhead_us`
  Defined as the timestamp taken immediately after a successful `VIDIOC_DQBUF` minus the timestamp taken immediately before invoking the frame callback. This isolates the CameraController-side software cost between dequeue and user callback delivery.

These two metrics answer different questions:

- Stable `frame_interval_us` close to the sensor/ISP period shows the capture pipeline is not stretching the frame cadence.
- Small `controller_overhead_us` shows the CameraController implementation itself contributes little extra software delay.

## Figure 3-6 Recommendation

Use a mixed board layout with one row per resolution, for example `640x480`, `1280x720`, `1920x1080`.

Each row contains:

1. A 2x2 real-frame mosaic
   - Use four sampled frames from the same run
   - Overlay frame index and measured interval
2. A short-window time-series plot of `frame_interval_us`
   - Show the first 100 to 200 frames
   - Makes jitter and periodic spikes visible
3. A distribution plot
   - Prefer box plot or violin plot
   - Plot both `frame_interval_us` and `controller_overhead_us`
4. A small statistics block
   - `mean / median / std / p95 / p99 / min / max / dropped`

This gives the visual density your advisor wants while still being technically defensible.

## Example Layout

- `examples/camera_perf/camera_capture_perf.cpp`
  Main benchmark runner
- `examples/camera_perf/plot_camera_perf.py`
  Reads CSV and generates the paper figures
- `examples/camera_perf/README.md`
  Documents commands, parameters, and recommended paper workflow
- `examples/camera_perf/sample_config.md`
  Optional note for suggested resolution/frame-count combinations

## Data Outputs

For each run the C++ example writes:

- `metrics.csv`
  Per-frame metrics and run metadata
- `summary.txt`
  Human-readable statistics
- `frames/`
  A small set of sampled frame dumps suitable for the mosaic figure

The Python script reads one or more CSV files and produces:

- `figure_3_6_board.png`
- `frame_interval_timeseries.png`
- `latency_distribution.png`
- `summary_table.csv`

## Scope Control

Only the new camera performance example folder is added. Existing examples are not deleted unless they are directly blocking build clarity in this branch. The folder structure itself is used to keep the example area organized without unnecessary churn.

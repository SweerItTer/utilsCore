# Camera Capture Performance Benchmark

This directory keeps the camera capture benchmark, raw frame samples, and plotting scripts together so the experiment assets stay isolated from the other utils demos.

## Output

The benchmark writes into one run directory:

- `metrics.csv`: one row per measured frame
- `summary.txt`: requested and actual resolution plus callback-interval statistics
- `sample_frame_XXXX.<pixel_format>`: raw snapshots for figure mosaics

## Recommended run matrix

- `854x480`
- `960x540`
- `1280x720`
- `1600x900`
- `1920x1080`
- `2560x1440`
- `3200x1800`
- `3840x2160`

For each resolution, collect `300` to `1000` measured frames after a short warmup.

## Example

```bash
./Camera_Capture_Perf \
  --device /dev/video0 \
  --width 1920 \
  --height 1080 \
  --format nv12 \
  --frames 600 \
  --warmup 60 \
  --output-dir ./runs/1920x1080
```

## Matrix Run

```bash
python3 examples/camera_perf/run_camera_perf_matrix.py \
  --binary ./Camera_Capture_Perf \
  --device /dev/video0 \
  --output-dir ./runs/std16x9
```

## Plotting

Generate the paper-style mixed boards after pulling the supported run directories back to the host:

```bash
python3 examples/camera_perf/plot_camera_perf.py \
  --inputs \
  ./runs/std16x9/854x480 \
  ./runs/std16x9/960x540 \
  ./runs/std16x9/1280x720 \
  ./runs/std16x9/1600x900 \
  ./runs/std16x9/1920x1080 \
  ./runs/std16x9/2560x1440 \
  ./runs/std16x9/3200x1800 \
  ./runs/std16x9/3840x2160 \
  --output-dir ./figures
```

If you want the frame samples to have zero impact on latency statistics, run one pass with `--sample-count 0` for the metrics and a second short pass with samples enabled for the figure images.

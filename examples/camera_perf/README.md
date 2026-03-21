# Camera Capture Performance Benchmark

This directory keeps the camera capture benchmark, raw frame samples, and plotting script together so the experiment assets stay isolated from the other utils demos.

## Output

The benchmark writes into one run directory:

- `metrics.csv`: one row per measured frame
- `summary_frame_interval.txt`: aggregate callback-interval statistics
- `summary_dequeue_interval.txt`: aggregate dequeue-interval statistics
- `summary_controller_overhead.txt`: aggregate software-overhead statistics
- `samples/frame_XXXX_id_*.pgm`: grayscale snapshots for figure mosaics

## Recommended run matrix

- `640x480`
- `1280x720`
- `1920x1080`

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
  --output-dir ./runs/1080p
```

## Plotting

Generate the paper-style mixed boards after pulling the run directories back to the host:

```bash
python3 examples/camera_perf/plot_camera_perf.py \
  ./runs/480p ./runs/720p ./runs/1080p \
  --output ./figures/camera_perf_board.png
```

If you want the frame samples to have zero impact on latency statistics, run one pass with `--sample-count 0` for the metrics and a second short pass with samples enabled for the figure images.

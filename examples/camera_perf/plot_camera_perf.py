#!/usr/bin/env python3

import argparse
import csv
import statistics
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt


def parse_summary(path: Path):
    data = {}
    with path.open("r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or "=" not in line:
                continue
            key, value = line.split("=", 1)
            data[key] = value
    return data


def read_metrics(path: Path):
    rows = []
    with path.open("r", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows.append(
                {
                    "frame_index": int(row["frame_index"]),
                    "frame_interval_us": int(row["frame_interval_us"]),
                }
            )
    return rows


def percentile(values, ratio):
    if not values:
        return 0
    ordered = sorted(values)
    index = round((len(ordered) - 1) * ratio)
    return ordered[index]


def convert_raw_sample(sample_path: Path, width: int, height: int, pixel_format: str, output_dir: Path):
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"{sample_path.stem}.png"
    cmd = [
        "ffmpeg",
        "-y",
        "-f",
        "rawvideo",
        "-pixel_format",
        pixel_format,
        "-video_size",
        f"{width}x{height}",
        "-i",
        str(sample_path),
        "-frames:v",
        "1",
        str(output_path),
    ]
    subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return output_path


def draw_board(run_dirs, output_dir: Path):
    output_dir.mkdir(parents=True, exist_ok=True)
    preview_dir = output_dir / "previews"
    preview_dir.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(len(run_dirs), 4, figsize=(18, 4.8 * len(run_dirs)))
    if len(run_dirs) == 1:
        axes = [axes]

    for row_axes, run_dir in zip(axes, run_dirs):
        summary = parse_summary(run_dir / "summary.txt")
        metrics = read_metrics(run_dir / "metrics.csv")
        width = int(summary["actual_width"])
        height = int(summary["actual_height"])
        requested_label = f"{summary['requested_width']}x{summary['requested_height']}"
        actual_label = f"{width}x{height}"
        interval_values = [row["frame_interval_us"] for row in metrics if row["frame_interval_us"] > 0]

        sample_files = sorted(run_dir.glob(f"sample_frame_*.{summary['pixel_format']}"))
        if sample_files:
            preview = convert_raw_sample(sample_files[0], width, height, summary["pixel_format"], preview_dir)
            image = plt.imread(preview)
            row_axes[0].imshow(image)
            row_axes[0].set_title(f"{requested_label} sample")
        else:
            row_axes[0].text(0.5, 0.5, "no sample", ha="center", va="center")
        row_axes[0].axis("off")

        row_axes[1].plot([row["frame_index"] for row in metrics], [row["frame_interval_us"] for row in metrics], linewidth=1.0)
        row_axes[1].set_title(f"{requested_label} frame interval")
        row_axes[1].set_xlabel("Frame index")
        row_axes[1].set_ylabel("us")

        row_axes[2].hist(interval_values, bins=30)
        row_axes[2].set_title(f"{requested_label} distribution")
        row_axes[2].set_xlabel("frame interval us")
        row_axes[2].set_ylabel("count")

        stats_lines = [
            f"requested: {requested_label}",
            f"actual:    {actual_label}",
            f"status:    {summary.get('status', 'unknown')}",
            f"mean:      {statistics.mean(interval_values):.2f} us" if interval_values else "mean:      n/a",
            f"median:    {statistics.median(interval_values):.2f} us" if interval_values else "median:    n/a",
            f"stddev:    {statistics.pstdev(interval_values):.2f} us" if len(interval_values) > 1 else "stddev:    n/a",
            f"p95:       {percentile(interval_values, 0.95)} us",
            f"p99:       {percentile(interval_values, 0.99)} us",
            f"rows:      {len(metrics)}",
        ]
        row_axes[3].axis("off")
        row_axes[3].text(0.02, 0.98, "\n".join(stats_lines), ha="left", va="top", family="monospace")

    fig.tight_layout()
    board_path = output_dir / "camera_perf_board.png"
    fig.savefig(board_path, dpi=180)
    print(f"wrote {board_path}")


def main():
    parser = argparse.ArgumentParser(description="Generate mixed camera performance figure boards.")
    parser.add_argument("--inputs", nargs="+", required=True, help="Run directories containing metrics.csv and summary.txt")
    parser.add_argument("--output-dir", required=True, help="Directory for generated figures")
    args = parser.parse_args()

    run_dirs = [Path(item) for item in args.inputs]
    draw_board(run_dirs, Path(args.output_dir))


if __name__ == "__main__":
    main()

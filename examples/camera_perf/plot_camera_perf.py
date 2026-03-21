#!/usr/bin/env python3

import argparse
import csv
import math
import os
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
                    "controller_overhead_us": int(row["controller_overhead_us"]),
                }
            )
    return rows


def percentile(values, ratio):
    if not values:
        return 0
    ordered = sorted(values)
    index = round((len(ordered) - 1) * ratio)
    return ordered[index]


def ensure_dir(path: Path):
    path.mkdir(parents=True, exist_ok=True)


def convert_raw_sample(sample_path: Path, width: int, height: int, pixel_format: str, output_dir: Path):
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
    ensure_dir(output_dir)
    preview_dir = output_dir / "previews"
    ensure_dir(preview_dir)

    fig, axes = plt.subplots(len(run_dirs), 4, figsize=(18, 5 * len(run_dirs)))
    if len(run_dirs) == 1:
        axes = [axes]

    for row_axes, run_dir in zip(axes, run_dirs):
        summary = parse_summary(run_dir / "summary.txt")
        metrics = read_metrics(run_dir / "metrics.csv")
        label = f"{summary['width']}x{summary['height']}"

        interval_values = [row["frame_interval_us"] for row in metrics if row["frame_interval_us"] > 0]
        overhead_values = [row["controller_overhead_us"] for row in metrics]

        sample_files = sorted(run_dir.glob(f"sample_frame_*.{summary['pixel_format']}"))
        if sample_files:
            preview = convert_raw_sample(
                sample_files[0],
                int(summary["width"]),
                int(summary["height"]),
                summary["pixel_format"],
                preview_dir,
            )
            image = plt.imread(preview)
            row_axes[0].imshow(image)
            row_axes[0].set_title(f"{label} sample")
        else:
            row_axes[0].text(0.5, 0.5, "no sample", ha="center", va="center")
        row_axes[0].axis("off")

        row_axes[1].plot([row["frame_index"] for row in metrics], [row["frame_interval_us"] for row in metrics], linewidth=1.0)
        row_axes[1].set_title(f"{label} frame interval")
        row_axes[1].set_xlabel("Frame index")
        row_axes[1].set_ylabel("us")

        row_axes[2].boxplot([interval_values, overhead_values], labels=["interval", "overhead"])
        row_axes[2].set_title(f"{label} distribution")
        row_axes[2].set_ylabel("us")

        stats_text = "\n".join(
            [
                f"interval mean: {statistics.mean(interval_values):.2f} us" if interval_values else "interval mean: n/a",
                f"interval p95: {percentile(interval_values, 0.95)} us",
                f"interval p99: {percentile(interval_values, 0.99)} us",
                f"overhead mean: {statistics.mean(overhead_values):.2f} us" if overhead_values else "overhead mean: n/a",
                f"overhead p95: {percentile(overhead_values, 0.95)} us",
                f"rows: {len(metrics)}",
            ]
        )
        row_axes[3].axis("off")
        row_axes[3].text(0.02, 0.98, stats_text, ha="left", va="top", family="monospace")

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
    output_dir = Path(args.output_dir)
    draw_board(run_dirs, output_dir)


if __name__ == "__main__":
    main()


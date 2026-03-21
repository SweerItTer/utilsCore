#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties


CJK_FONT = FontProperties(fname="/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
RESOLUTION_ORDER = [
    "1280x720",
    "1600x900",
    "1920x1080",
    "2560x1440",
    "3200x1800",
    "3840x2160",
]
COLORS = ["#5b8ff9", "#61d9a5", "#65789b", "#f6bd16", "#7262fd", "#78d3f8"]


def read_metrics(path: Path):
    rows = []
    with path.open("r", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            interval_us = int(row["frame_interval_us"])
            if interval_us <= 0:
                continue
            rows.append(
                {
                    "frame_index": int(row["frame_index"]),
                    "frame_id": row["frame_id"],
                    "v4l2_timestamp_ns": row["v4l2_timestamp_ns"],
                    "callback_timestamp_us": row["callback_timestamp_us"],
                    "frame_interval_ms": interval_us / 1000.0,
                    "buffer_index": row["buffer_index"],
                    "payload_bytes": row["payload_bytes"],
                }
            )
    return rows


def load_runs(run_dirs):
    by_name = {Path(run_dir).name: Path(run_dir) for run_dir in run_dirs}
    runs = []
    for color, resolution in zip(COLORS, RESOLUTION_ORDER):
        run_dir = by_name.get(resolution)
        if run_dir is None:
            continue
        metrics = read_metrics(run_dir / "metrics.csv")
        if not metrics:
            continue
        runs.append({"resolution": resolution, "color": color, "metrics": metrics})
    if len(runs) != 6:
        raise RuntimeError("expected 6 run directories covering 720p to 2160p")
    return runs


def draw_line_board(runs, output_dir: Path):
    fig, axes = plt.subplots(3, 2, figsize=(13.5, 9.5), constrained_layout=True)
    axes = axes.flatten()

    y_values = [row["frame_interval_ms"] for run in runs for row in run["metrics"]]
    mean_ms = sum(y_values) / len(y_values)
    y_min = min(y_values)
    y_max = max(y_values)
    pad = max((y_max - y_min) * 0.08, 0.05)

    for index, run in enumerate(runs):
        ax = axes[index]
        x = [row["frame_index"] for row in run["metrics"]]
        y = [row["frame_interval_ms"] for row in run["metrics"]]
        ax.plot(x, y, color=run["color"], linewidth=1.15)
        ax.axhline(mean_ms, color="#c62828", linewidth=2.6)
        ax.set_title(run["resolution"], color=run["color"], fontsize=12, pad=8)
        ax.set_ylim(y_min - pad, y_max + pad)
        ax.grid(axis="y", alpha=0.22, linewidth=0.6)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)
        if index >= 4:
            ax.set_xlabel("分辨率", fontproperties=CJK_FONT)
        else:
            ax.set_xlabel("")
        ax.set_ylabel("")

    board_path = output_dir / "camera_perf_board.png"
    fig.savefig(board_path, dpi=240, bbox_inches="tight")
    print(f"wrote {board_path}")


def draw_table_board(runs, output_dir: Path, rows_per_resolution: int = 6):
    fig, axes = plt.subplots(3, 2, figsize=(15, 10), constrained_layout=True)
    axes = axes.flatten()

    headers = ["frame", "frame_id", "callback_us", "interval_ms", "buf", "bytes"]

    for index, run in enumerate(runs):
        ax = axes[index]
        ax.axis("off")
        sample_rows = run["metrics"][:rows_per_resolution]
        table_rows = [
            [
                str(row["frame_index"]),
                str(row["frame_id"]),
                str(row["callback_timestamp_us"]),
                f"{row['frame_interval_ms']:.3f}",
                str(row["buffer_index"]),
                str(row["payload_bytes"]),
            ]
            for row in sample_rows
        ]
        table = ax.table(
            cellText=table_rows,
            colLabels=headers,
            loc="center",
            cellLoc="center",
            colLoc="center",
        )
        table.auto_set_font_size(False)
        table.set_fontsize(7.5)
        table.scale(1.0, 1.28)
        for (row, col), cell in table.get_celld().items():
            cell.set_linewidth(0.35)
            if row == 0:
                cell.set_facecolor("#e8eef5")
                cell.set_text_props(weight="bold")
            else:
                cell.set_facecolor("#ffffff" if row % 2 else "#f9fbfd")
        ax.set_title(run["resolution"], color=run["color"], fontsize=12, pad=8)

    table_path = output_dir / "camera_perf_table.png"
    fig.savefig(table_path, dpi=240, bbox_inches="tight")
    print(f"wrote {table_path}")


def main():
    parser = argparse.ArgumentParser(description="Generate 3x2 camera performance line board and table board.")
    parser.add_argument("--inputs", nargs="+", required=True, help="Run directories containing metrics.csv")
    parser.add_argument("--output-dir", required=True, help="Directory for generated figures")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    runs = load_runs([Path(item) for item in args.inputs])
    draw_line_board(runs, output_dir)
    draw_table_board(runs, output_dir)


if __name__ == "__main__":
    main()

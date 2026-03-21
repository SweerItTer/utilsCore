#!/usr/bin/env python3

import argparse
import csv
import subprocess
from pathlib import Path


STANDARD_16X9_RESOLUTIONS = [
    (854, 480),
    (960, 540),
    (1280, 720),
    (1600, 900),
    (1920, 1080),
    (2560, 1440),
    (3200, 1800),
    (3840, 2160),
]


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


def build_command(args, width, height, output_dir):
    return [
        args.binary,
        "--device",
        args.device,
        "--width",
        str(width),
        "--height",
        str(height),
        "--format",
        args.pixel_format,
        "--frames",
        str(args.frames),
        "--warmup",
        str(args.warmup),
        "--buffer-count",
        str(args.buffer_count),
        "--sample-count",
        str(args.sample_count),
        "--use-dmabuf",
        "true" if args.use_dmabuf else "false",
        "--output-dir",
        str(output_dir),
    ]


def main():
    parser = argparse.ArgumentParser(description="Run Camera_Capture_Perf across standard 16:9 resolutions from 480p to 4K.")
    parser.add_argument("--binary", default="./Camera_Capture_Perf", help="Path to Camera_Capture_Perf")
    parser.add_argument("--device", default="/dev/video0", help="Camera device path")
    parser.add_argument("--output-dir", required=True, help="Base output directory")
    parser.add_argument("--frames", type=int, default=300, help="Measured frames per resolution")
    parser.add_argument("--warmup", type=int, default=30, help="Warmup frames per resolution")
    parser.add_argument("--buffer-count", type=int, default=4, help="V4L2 buffer count")
    parser.add_argument("--sample-count", type=int, default=4, help="Saved sample frames per resolution")
    parser.add_argument("--pixel-format", default="nv12", help="Capture pixel format")
    parser.add_argument("--use-dmabuf", action="store_true", help="Use DMABUF capture mode")
    args = parser.parse_args()

    base_output = Path(args.output_dir)
    base_output.mkdir(parents=True, exist_ok=True)
    matrix_rows = []

    for width, height in STANDARD_16X9_RESOLUTIONS:
        run_dir = base_output / f"{width}x{height}"
        run_dir.mkdir(parents=True, exist_ok=True)
        result = subprocess.run(build_command(args, width, height, run_dir), text=True, capture_output=True)

        if result.returncode != 0:
            matrix_rows.append(
                {
                    "requested": f"{width}x{height}",
                    "actual": "",
                    "status": "failed",
                    "note": result.stderr.strip() or result.stdout.strip(),
                }
            )
            continue

        summary = parse_summary(run_dir / "summary.txt")
        matrix_rows.append(
            {
                "requested": f"{width}x{height}",
                "actual": f"{summary.get('actual_width', '0')}x{summary.get('actual_height', '0')}",
                "status": summary.get("status", "unknown"),
                "note": "",
            }
        )

    matrix_path = base_output / "matrix_status.csv"
    with matrix_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["requested", "actual", "status", "note"])
        writer.writeheader()
        writer.writerows(matrix_rows)

    print(f"wrote {matrix_path}")


if __name__ == "__main__":
    main()

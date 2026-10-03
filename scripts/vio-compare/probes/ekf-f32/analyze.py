#!/usr/bin/env python3
"""Compare three A53 EKF matrix-assembly runs on identical input."""

import csv
import json
import math
import statistics
import sys
from pathlib import Path


def percentile(values, percent):
    values = sorted(values)
    return values[round((len(values) - 1) * percent / 100)] if values else None


def load(root, mode):
    trajectory = {}
    camera_ms = []
    with (root / f"trajectory-ekf-{mode}.csv").open() as file:
        for row in csv.DictReader(file):
            camera_ms.append(float(row["compute_ms"]))
            if row["initialized"] == "1":
                trajectory[row["timestamp_ns"]] = (
                    [float(row[key]) for key in ("px", "py", "pz")],
                    [float(row[key]) for key in ("qx", "qy", "qz", "qw")],
                )
    ma_ms = []
    with (root / f"ekf-{mode}.log").open() as file:
        for line in file:
            if line.startswith("EKF_MA "):
                ma_ms.append(float(line.split()[5]))
    return trajectory, camera_ms, ma_ms


def compare(a, b):
    common = sorted(a.keys() & b.keys())
    pos_delta = []
    rot_delta = []
    for stamp in common:
        p, q = a[stamp]
        r, s = b[stamp]
        pos_delta.append(math.dist(p, r))
        dot = min(1.0, abs(sum(x * y for x, y in zip(q, s))))
        rot_delta.append(math.degrees(2 * math.acos(dot)))
    return {
        "common_initialized_frames": len(common),
        "position_difference_median_m": statistics.median(pos_delta) if common else None,
        "position_difference_p95_m": percentile(pos_delta, 95),
        "position_difference_final_m": pos_delta[-1] if common else None,
        "attitude_difference_p95_deg": percentile(rot_delta, 95),
        "attitude_difference_final_deg": rot_delta[-1] if common else None,
    }


def main():
    root = Path(sys.argv[1])
    runs = {mode: load(root, mode) for mode in ("original", "packed_double", "packed_float")}
    report = {}
    for mode, (trajectory, camera_ms, ma_ms) in runs.items():
        report[mode] = {
            "initialized_frames": len(trajectory),
            "camera_mean_ms": statistics.mean(camera_ms),
            "ma_calls": len(ma_ms),
            "ma_total_ms": sum(ma_ms),
            "ma_mean_ms": statistics.mean(ma_ms) if ma_ms else None,
            "ma_p95_ms": percentile(ma_ms, 95),
        }
        if mode != "original":
            report[mode]["vs_original"] = compare(runs["original"][0], trajectory)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

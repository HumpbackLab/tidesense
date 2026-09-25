#!/usr/bin/env python3
"""Summarize A53 OpenVINS LK-detail replay logs and trajectory CSVs."""
import csv
import statistics as stats
import sys
from pathlib import Path


def mean(values):
    return stats.mean(values) if values else 0.0


def summary(log_path):
    groups = {kind: [] for kind in ("temporal", "stereo", "cross")}
    good = []
    total_frames = 0
    for line in log_path.read_text(errors="replace").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "LK_DETAIL" and fields[1] in groups:
            groups[fields[1]].append(fields)
        elif fields[0] == "LK_RESULT":
            good.append(int(fields[2]) + int(fields[3]))
        elif fields[0] == "DONE":
            total_frames = int(fields[1].split("=")[1])

    path = log_path.with_name(log_path.name.replace("lkdetail-", "trajectory-").replace(".log", ".csv"))
    initialized = []
    camera_ms = []
    if path.exists():
        with path.open() as stream:
            for row in csv.reader(stream):
                if row and row[0] != "timestamp_ns" and len(row) >= 10 and row[1] == "1":
                    initialized.append(tuple(float(x) for x in row[2:5]))
                    camera_ms.append(float(row[-1]))

    print(log_path.name, "frames", total_frames, "initialized", len(initialized),
          "good_LR", round(mean(good), 1),
          "camera_ms", round(mean(camera_ms), 2))
    for kind, rows in groups.items():
        if not rows:
            continue
        count = len(rows)
        per_frame = count / total_frames if total_frames else 0
        ms = [sum(float(r[j]) for r in rows) / total_frames for j in (6, 7, 8)] if total_frames else [0] * 3
        print(" ", kind, "calls", count, "per_frame", round(per_frame, 2),
              "points_per_call", round(mean([int(r[3]) for r in rows]), 1),
              "lk/undistort/ransac_ms_per_frame", *(round(x, 2) for x in ms))
    if initialized:
        start, end = initialized[0], initialized[-1]
        distance = sum((b-a)**2 for a, b in zip(start, end)) ** 0.5
        print("  first_last_distance_m", round(distance, 2))


for argument in sys.argv[1:]:
    summary(Path(argument))

#!/usr/bin/env python3
"""Summarize VINS-Fusion body pose trajectory against its first valid pose."""
import csv
import math
import sys

rows = list(csv.DictReader(open(sys.argv[1], newline="")))
times = [float(row["timestamp_s"]) for row in rows]
positions = [tuple(float(row[key]) for key in ("x", "y", "z")) for row in rows]
origin = positions[0]
distances = [math.dist(origin, p) for p in positions]
record_start_s = 3090171699390 * 1e-9
print("poses", len(rows))
print("initial_frame", rows[0]["frame"])
print("max_displacement_record_first_15_s_m",
      max(d for d, t in zip(distances, times) if t <= record_start_s + 15))
print("max_displacement_first_valid_pose_plus_15_s_m",
      max(d for d, t in zip(distances, times) if t <= times[0] + 15))
print("final_relative_displacement_m", distances[-1])
print("max_relative_displacement_m", max(distances))
print("path_length_m", sum(math.dist(a, b) for a, b in zip(positions, positions[1:])))
print("final_xyz_m", positions[-1])

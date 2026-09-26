#!/usr/bin/env python3
"""Summarize the three maintained host replays, with position-only GT ATE."""
import csv
import json
import os
from pathlib import Path

import numpy as np

root = Path(__file__).resolve().parents[3]
work = Path(os.environ.get("VIO_COMPARE_WORK", root / "install/vio-compare-20260924-121320"))
results = work / "ov-schurvins/results"


def trajectory(path, offset_ns=0):
    rows = list(csv.DictReader(path.open()))
    complete = [r for r in rows if r["initialized"] == "1" and r["compute_ms"] and
                all(r[k] for k in ("px", "py", "pz"))]
    pose = {int(r["timestamp_ns"]) + offset_ns: np.array([float(r[k]) for k in ("px", "py", "pz")])
            for r in complete}
    times = [float(r["compute_ms"]) for r in complete]
    return rows, pose, times


def truth(path):
    values = np.loadtxt(path, delimiter=",", comments="#")
    return values[:, 0].astype(np.int64), values[:, 1:4]


def assess(estimated, gt_times, gt_positions):
    timestamps = np.array(sorted(t for t in estimated if gt_times[0] <= t <= gt_times[-1]), dtype=np.int64)
    if len(timestamps) < 20:
        return {"gt_overlap_frames": int(len(timestamps))}
    measured = np.array([estimated[int(t)] for t in timestamps])
    expected = np.column_stack([np.interp(timestamps, gt_times, gt_positions[:, i]) for i in range(3)])
    u, _, vh = np.linalg.svd((measured-measured.mean(0)).T @ (expected-expected.mean(0)))
    correction = np.eye(3)
    correction[-1, -1] = np.linalg.det(u @ vh)
    aligned = (measured-measured.mean(0)) @ (u @ correction @ vh) + expected.mean(0)
    errors = np.linalg.norm(aligned - expected, axis=1)
    return {"gt_overlap_frames": int(len(timestamps)), "gt_span_s": float((timestamps[-1]-timestamps[0])*1e-9),
            "ate_rmse_m": float(np.sqrt(np.mean(errors**2))), "ate_p95_m": float(np.percentile(errors, 95)),
            "final_gt_overlap_error_m": float(errors[-1])}


gt = {
    "uzh": truth(root / "install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv"),
    "tum": truth(root / "install/fisheye-benchmark/datasets/dataset-room1_512_16/mav0/mocap0/data.csv"),
}
report = {}
for name in ("uzh", "tum", "tidesense"):
    path = results / name / "trajectory.csv"
    if not path.exists():
        continue
    offset = -16_684_572 if name == "uzh" else 0
    rows, pose, timings = trajectory(path, offset)
    log = results / name / "run.log"
    output = {"frames": len(rows), "initialized_frames": len(pose),
              "completed": any(line.startswith("DONE ") for line in log.open(errors="replace"))}
    if timings:
        output.update(camera_ms_mean=float(np.mean(timings)), camera_ms_p95=float(np.percentile(timings, 95)))
    if pose:
        positions = list(pose.values())
        output["first_last_displacement_m"] = float(np.linalg.norm(positions[-1]-positions[0]))
    if name in gt:
        output.update(assess(pose, *gt[name]))
    matched = results / name / "openvins-matched.csv"
    if matched.exists():
        matched_rows, matched_pose, matched_times = trajectory(matched, offset)
        baseline = {"frames": len(matched_rows), "initialized_frames": len(matched_pose)}
        if matched_times:
            baseline["camera_ms_mean"] = float(np.mean(matched_times))
        if matched_pose:
            baseline["first_last_displacement_m"] = float(
                np.linalg.norm(list(matched_pose.values())[-1] - list(matched_pose.values())[0]))
        if name in gt:
            baseline.update(assess(matched_pose, *gt[name]))
        output["openvins_matched_config"] = baseline
    report[name] = output
(results / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

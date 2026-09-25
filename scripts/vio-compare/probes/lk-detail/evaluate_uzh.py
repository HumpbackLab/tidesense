#!/usr/bin/env python3
"""Compare UZH-FPV OpenVINS probe variants on identical GT-covered frames."""
import csv
import json
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "install/fisheye-benchmark/openvins/lk-detail-uzh"
GT = ROOT / "install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv"
SHIFT_NS = -16_684_572  # Official cam0 timeshift_cam_imu.
NAMES = ("noimu-default", "imu-default", "imu-l3", "imu-r200", "imu-combo", "noimu-combo")


def poses(name):
    result = {}
    times = []
    with (OUT / f"trajectory-{name}.csv").open() as stream:
        for row in csv.DictReader(stream):
            if row["initialized"] != "1":
                continue
            timestamp = int(row["timestamp_ns"]) + SHIFT_NS
            result[timestamp] = np.array([float(row[k]) for k in ("px", "py", "pz")])
            times.append(float(row["compute_ms"]))
    return result, times


def stage_summary(name):
    stages = {"temporal": np.zeros(3), "stereo": np.zeros(3)}
    good = []
    frame_count = 0
    with (OUT / f"lkdetail-{name}.log").open(errors="replace") as stream:
        for line in stream:
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "LK_DETAIL" and fields[1] in stages:
                stages[fields[1]] += [float(x) for x in fields[6:9]]
            elif fields[0] == "LK_RESULT":
                good.append(int(fields[2]) + int(fields[3]))
            elif fields[0] == "DONE":
                frame_count = int(fields[1].split("=")[1])
    return {
        "frames": frame_count,
        "good_features_mean": float(np.mean(good)),
        "frames_good_below_40": sum(x < 40 for x in good),
        "stage_ms_per_frame": {kind: (values / frame_count).tolist() for kind, values in stages.items()},
    }


data = {name: poses(name) for name in NAMES}
gt = np.loadtxt(GT, delimiter=",", comments="#")
times = sorted(set.intersection(*(set(points) for points, _ in data.values())))
times = np.array([t for t in times if gt[0, 0] <= t <= gt[-1, 0]], dtype=np.int64)
if len(times) < 100:
    raise RuntimeError("Insufficient common GT-covered initialized frames")
truth = np.column_stack([np.interp(times, gt[:, 0], gt[:, k]) for k in (1, 2, 3)])

report = {"sequence": "UZH-FPV indoor_forward_6", "common_gt_frames": len(times),
          "common_gt_span_s": float((times[-1]-times[0])*1e-9), "variants": {}}
for name in NAMES:
    points, compute = data[name]
    estimate = np.array([points[int(t)] for t in times])
    xmean, ymean = estimate.mean(0), truth.mean(0)
    u, _, vt = np.linalg.svd((estimate-xmean).T @ (truth-ymean))
    correction = np.eye(3)
    correction[-1, -1] = np.linalg.det(u @ vt)
    aligned = (estimate-xmean) @ (u @ correction @ vt) + ymean
    error = np.linalg.norm(aligned-truth, axis=1)
    report["variants"][name] = {
        **stage_summary(name),
        "initialized_frames": len(points),
        "camera_ms_mean_initialized": float(np.mean(compute)),
        "camera_ms_p95_initialized": float(np.percentile(compute, 95)),
        "ate_rmse_m": float(np.sqrt(np.mean(error**2))),
        "ate_p95_m": float(np.percentile(error, 95)),
        "ate_final_common_m": float(error[-1]),
    }

(OUT / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

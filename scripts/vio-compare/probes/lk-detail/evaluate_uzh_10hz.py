#!/usr/bin/env python3
"""Compare 10 Hz camera replay against 30 Hz on UZH-FPV indoor_forward_6."""
import csv
import json
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation


ROOT = Path(__file__).resolve().parents[4]
BASE = ROOT / "install/fisheye-benchmark/openvins"
OUT = BASE / "uzh-10hz"
GT = ROOT / "install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv"
SHIFT_NS = -16_684_572
PATHS = {
    "baseline_30hz": BASE / "lk-detail-uzh/trajectory-noimu-default.csv",
    "baseline_10hz": OUT / "trajectory-baseline.csv",
    "imu_default_30hz": BASE / "lk-detail-uzh/trajectory-imu-default.csv",
    "imu_default_10hz": OUT / "trajectory-imu-default.csv",
    "optimized_30hz": BASE / "lk-detail-uzh/trajectory-imu-r200.csv",
    "optimized_10hz": OUT / "trajectory-optimized.csv",
    "combo_30hz": BASE / "lk-detail-uzh/trajectory-imu-combo.csv",
    "combo_10hz": OUT / "trajectory-combo.csv",
}


def load(path):
    rows = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            if row["initialized"] == "1":
                rows[int(row["timestamp_ns"])] = row
    return rows


def position(row):
    return np.array([float(row[k]) for k in ("px", "py", "pz")])


def orientation(row):
    # OpenVINS q_GtoI is a JPL quaternion; SciPy uses Hamilton xyzw.
    quat = [-float(row[k]) for k in ("qx", "qy", "qz")] + [float(row["qw"])]
    return Rotation.from_quat(quat).as_matrix()


data = {name: load(path) for name, path in PATHS.items()}
times = sorted(set.intersection(*(set(rows) for rows in data.values())))
gt = np.loadtxt(GT, delimiter=",", comments="#")
gt_times = [t for t in times if gt[0, 0] <= t + SHIFT_NS <= gt[-1, 0]]
if len(gt_times) < 100:
    raise RuntimeError("Insufficient common GT-covered frames")
shifted = np.array([t + SHIFT_NS for t in gt_times], dtype=np.int64)
truth = np.column_stack([np.interp(shifted, gt[:, 0], gt[:, k]) for k in (1, 2, 3)])

report = {"sequence": "UZH-FPV indoor_forward_6", "common_initialized_frames": len(times),
          "common_gt_frames": len(gt_times), "gt_span_s": float((gt_times[-1]-gt_times[0])*1e-9),
          "variants": {}, "rate_orientation_difference_deg": {}}
for name, rows in data.items():
    estimate = np.array([position(rows[t]) for t in gt_times])
    xmean, ymean = estimate.mean(0), truth.mean(0)
    u, _, vt = np.linalg.svd((estimate-xmean).T @ (truth-ymean))
    correction = np.eye(3)
    correction[-1, -1] = np.linalg.det(u @ vt)
    aligned = (estimate-xmean) @ (u @ correction @ vt) + ymean
    error = np.linalg.norm(aligned-truth, axis=1)
    report["variants"][name] = {
        "initialized_frames": len(rows),
        "ate_rmse_m": float(np.sqrt(np.mean(error**2))),
        "ate_p95_m": float(np.percentile(error, 95)),
        "ate_final_gt_frame_m": float(error[-1]),
        "compute_ms_mean": float(np.mean([float(row["compute_ms"]) for row in rows.values()])),
    }

for group in ("baseline", "imu_default", "optimized", "combo"):
    full, slow = data[group+"_30hz"], data[group+"_10hz"]
    first = times[0]
    # q_GtoI maps global into IMU, so align the arbitrary global frame on the right.
    frame_alignment = orientation(slow[first]).T @ orientation(full[first])
    angles = []
    for timestamp in times:
        delta = orientation(slow[timestamp]) @ frame_alignment @ orientation(full[timestamp]).T
        angles.append(np.rad2deg(Rotation.from_matrix(delta).magnitude()))
    report["rate_orientation_difference_deg"][group] = {
        "median": float(np.median(angles)),
        "p95": float(np.percentile(angles, 95)),
        "max": float(np.max(angles)),
        "final": float(angles[-1]),
    }

(OUT / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

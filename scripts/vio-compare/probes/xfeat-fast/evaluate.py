#!/usr/bin/env python3
"""Summarize board OpenVINS trajectories; align UZH position to GT without scale."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def load(path):
    rows = list(csv.DictReader(Path(path).open()))
    valid = [r for r in rows if r["initialized"] == "1"]
    pos = np.array([[float(r[k]) for k in ("px", "py", "pz")] for r in valid])
    time = np.array([int(r["timestamp_ns"]) for r in valid], dtype=np.int64)
    compute = np.array([float(r["compute_ms"]) for r in rows])
    result = {
        "frames": len(rows), "initialized_frames": len(valid),
        "first_initialized_frame": rows.index(valid[0]) if valid else None,
        "compute_ms_mean": float(compute.mean()),
        "compute_ms_p95": float(np.percentile(compute, 95)),
        "end_to_end_displacement_m": float(np.linalg.norm(pos[-1]-pos[0])) if len(pos) else None,
    }
    return time, pos, result


def ate(times, estimate, gt):
    ok = (times >= gt[0, 0]) & (times <= gt[-1, 0])
    times, estimate = times[ok], estimate[ok]
    if len(times) < 3:
        return {"gt_covered_frames": len(times)}
    base = int(gt[0, 0])
    truth = np.column_stack([np.interp((times-base)*1e-9, (gt[:, 0]-base)*1e-9, gt[:, k]) for k in (1, 2, 3)])
    ex, tx = estimate.mean(0), truth.mean(0)
    u, _, vt = np.linalg.svd((estimate-ex).T @ (truth-tx))
    fix = np.diag([1, 1, np.linalg.det(u @ vt)])
    aligned = (estimate-ex) @ (u @ fix @ vt) + tx
    error = np.linalg.norm(aligned-truth, axis=1)
    return {
        "gt_covered_frames": len(times),
        "gt_covered_span_s": float((times[-1]-times[0])*1e-9),
        "ate_rmse_m": float(np.sqrt(np.mean(error**2))),
        "ate_p95_m": float(np.percentile(error, 95)),
        "final_aligned_error_m": float(error[-1]),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--poses", type=Path, required=True)
    p.add_argument("--gt", type=Path)
    p.add_argument("--timestamp-shift-ns", type=int, default=0)
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    t, x, result = load(args.poses)
    t = t + args.timestamp_shift_ns
    if args.gt:
        gt = np.loadtxt(args.gt, delimiter=",", comments="#")
        result.update(ate(t, x, gt))
    report = json.dumps(result, indent=2)
    print(report)
    if args.out:
        args.out.write_text(report+"\n")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare two FPV trajectories on identical GT-covered image timestamps."""
import argparse
import csv
import json

import numpy as np


def load(path, shift):
    out = {}
    with open(path) as stream:
        for row in csv.DictReader(stream):
            if row["initialized"] == "1":
                out[int(row["timestamp_ns"])+shift] = np.array([float(row[k]) for k in ("px", "py", "pz")])
    return out


def align_error(est, gt):
    em, gm = est.mean(0), gt.mean(0)
    u, _, vt = np.linalg.svd((est-em).T @ (gt-gm))
    fix = np.diag([1, 1, np.linalg.det(u @ vt)])
    fit = (est-em) @ (u @ fix @ vt) + gm
    err = np.linalg.norm(fit-gt, axis=1)
    return {"rmse_m": float(np.sqrt(np.mean(err**2))),
            "p95_m": float(np.percentile(err, 95)),
            "last_m": float(err[-1])}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--xfeat", required=True)
    p.add_argument("--baseline", required=True)
    p.add_argument("--baseline-shift-ns", type=int, default=0)
    p.add_argument("--gt", required=True)
    args = p.parse_args()
    xfeat = load(args.xfeat, 0)
    baseline = load(args.baseline, args.baseline_shift_ns)
    gt = np.loadtxt(args.gt, delimiter=",", comments="#")
    times = np.array(sorted(t for t in xfeat.keys() & baseline.keys()
                            if gt[0, 0] <= t <= gt[-1, 0]), dtype=np.int64)
    if len(times) < 10:
        raise RuntimeError("fewer than ten common GT-covered poses")
    base = int(gt[0, 0])
    truth = np.column_stack([np.interp((times-base)*1e-9, (gt[:, 0]-base)*1e-9, gt[:, k]) for k in (1, 2, 3)])
    result = {"common_frames": len(times), "span_s": float((times[-1]-times[0])*1e-9),
              "xfeat": align_error(np.array([xfeat[t] for t in times]), truth),
              "baseline": align_error(np.array([baseline[t] for t in times]), truth)}
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()

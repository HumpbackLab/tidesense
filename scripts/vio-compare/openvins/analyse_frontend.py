#!/usr/bin/env python3
"""Summarize whole-frontend replay and the fixed RAM window (CPU versus wall)."""
import argparse
import csv
import json
from pathlib import Path
import re
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("results", type=Path)
p.add_argument("--gt", type=Path)
p.add_argument("--baseline-memory", type=Path)
a = p.parse_args()


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def summarize(rows):
    fields = [k for k in rows[0] if k.endswith("_ms")]
    out = {}
    for key in fields:
        v = np.array([float(r[key]) for r in rows])
        out[key] = dict(mean=float(v.mean()), p95=float(np.percentile(v, 95)), max=float(v.max()))
    return out


rows = read(a.results / "front.csv")
ready = [r for r in rows if r["initialized"] == "1"]
if not ready:
    raise RuntimeError("Replay did not initialize")
done = re.findall(r"DONE frames=(\d+).*?wall_s=([\d.]+)", (a.results / "front.log").read_text())
if not done or int(done[-1][0]) != len(rows):
    raise RuntimeError("Replay incomplete")
report = dict(frames=len(rows), initialized=len(ready), wall_s=float(done[-1][1]),
              initialized_timings=summarize(ready),
              frontend_over_50ms_fraction=float(np.mean([float(r["front_total_ms"]) > 50 for r in ready])))
if any(float(r["front_calls"]) != 1 for r in rows):
    raise RuntimeError("Expected exactly one frontend request per consumed frame")
if a.gt:
    gt = np.loadtxt(a.gt, delimiter=",", comments="#")
    selected = [r for r in ready if gt[0, 0] <= int(r["timestamp_ns"]) - 16684572 <= gt[-1, 0]]
    t = np.array([int(r["timestamp_ns"]) - 16684572 for r in selected])
    estimate = np.array([[float(r[k]) for k in ["px", "py", "pz"]] for r in selected])
    truth = np.column_stack([np.interp(t, gt[:, 0], gt[:, i]) for i in [1, 2, 3]])
    x, y = estimate.mean(0), truth.mean(0)
    u, _, vt = np.linalg.svd((estimate - x).T @ (truth - y))
    d = np.eye(3)
    d[-1, -1] = np.linalg.det(u @ vt)
    error = np.linalg.norm((estimate - x) @ (u @ d @ vt) + y - truth, axis=1)
    report["gt"] = dict(common_frames=len(t), shift_ns=-16684572, alignment="SE3, no scale",
                        ate_rmse_m=float(np.sqrt(np.mean(error**2))))
memory_path = a.results / "memory.csv"
if memory_path.exists():
    memory = read(memory_path)
    keys = ["initialized", "px", "py", "pz", "qx", "qy", "qz", "qw"]
    report["memory_poses_identical"] = all(all(x[k] == y[k] for k in keys) for x, y in zip(memory, rows))
    report["ram_window_1002_1038"] = summarize(memory[1002:1038])
    events = [e for e in read(a.results / "memory-trace.csv") if e["kind"] == "consume_wait"]
    starts = []
    for row in memory[1002:1039]:
        stamp = int(row["timestamp_ns"]) * 1e-9
        event = min(events, key=lambda e: abs(float(e["frame_stamp"]) - stamp))
        # Trace prints 15 significant digits: several microseconds rounding at Unix timestamps.
        if abs(float(event["frame_stamp"]) - stamp) > 1e-5:
            raise RuntimeError("Missing RAM window trace event")
        starts.append(float(event["begin_ms"]))
    period = np.diff(starts)
    report["ram_period_ms"] = dict(mean=float(period.mean()), p95=float(np.percentile(period, 95)),
                                    fps=float(1000 / period.mean()))
    if a.baseline_memory:
        report["baseline_same_window"] = summarize(read(a.baseline_memory)[1002:1038])
(a.results / "frontend-summary.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

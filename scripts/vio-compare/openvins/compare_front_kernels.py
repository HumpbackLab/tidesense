#!/usr/bin/env python3
"""Compare C906 micro-optimizations on an identical, initialized RAM input window."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("results", type=Path)
p.add_argument("--reference", type=Path, required=True)
p.add_argument("--modes", nargs="+", default=["baseline", "nms", "mask", "flow", "patch", "basecache", "group", "combined", "production"])
a = p.parse_args()


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f))


reference = read(a.reference)
keys = ["timestamp_ns", "initialized", "px", "py", "pz", "qx", "qy", "qz", "qw"]
report = {}
for mode in a.modes:
    path = a.results / (mode + ".csv")
    if not path.exists():
        continue
    rows = read(path)
    if len(rows) not in [1040, len(reference)] or len(rows)>len(reference) or not all(all(x[k] == y[k] for k in keys) for x, y in zip(rows, reference)):
        raise RuntimeError(mode + " incomplete or trajectory differs")
    if f"DONE frames={len(rows)} " not in (a.results / (mode + ".log")).read_text(errors="replace"):
        raise RuntimeError(mode + " did not finish cleanly")
    subset = rows[1002:1038]
    out = {"frames": len(rows), "poses_identical": True}
    for key in rows[0]:
        if key.endswith("_ms") or key.startswith("detail_"):
            v = np.array([float(r[key]) for r in subset])
            out[key] = dict(mean=float(v.mean()), p95=float(np.percentile(v, 95)))
    active = [float(r["front_total_ms"]) - float(r["front_snapshot_wait_ms"]) for r in subset]
    out["frontend_active_ms"] = dict(mean=float(np.mean(active)), p95=float(np.percentile(active, 95)))
    trace = read(a.results / (mode + "-trace.csv"))
    # Whole camera entrances remain comparable when work moves before the join.
    kind = "camera" if any(e["kind"] == "camera" for e in trace) else "consume_wait"
    events = [e for e in trace if e["kind"] == kind]
    out["period_trace"] = kind
    if "detail_intrinsics_version" in rows[0]:
        versions = [int(r["detail_intrinsics_version"]) for r in rows]
        if any(versions):
            if min(versions)<1 or any(x>y for x,y in zip(versions,versions[1:])):
                raise RuntimeError(mode + " calibration snapshot version regressed")
            out["intrinsics_versions"] = dict(min=min(versions),max=max(versions),monotonic=True)
    starts = []
    for r in rows[1002:1039]:
        stamp = int(r["timestamp_ns"]) * 1e-9
        e = min(events, key=lambda x: abs(float(x["frame_stamp"]) - stamp))
        if abs(float(e["frame_stamp"]) - stamp) > 1e-5:
            raise RuntimeError("Missing trace event")
        starts.append(float(e["begin_ms"]))
    periods = np.diff(starts)
    out["period_ms"] = dict(mean=float(periods.mean()), p95=float(np.percentile(periods, 95)))
    out["fps"] = float(1000 / periods.mean())
    report[mode] = out
(a.results / "kernel-comparison.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

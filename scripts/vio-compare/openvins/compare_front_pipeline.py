#!/usr/bin/env python3
"""Compare identical-input RAM windows; never infer throughput from SD-hidden waits."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("results", type=Path)
p.add_argument("--reference", type=Path, required=True)
a = p.parse_args()


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f))


reference = read(a.reference)
keys = ["initialized", "px", "py", "pz", "qx", "qy", "qz", "qw"]
report = {}
for mode in ["late", "early", "fast"]:
    path = a.results / (mode + ".csv")
    if not path.exists():
        continue
    rows = read(path)
    if len(rows) != 1040:
        raise RuntimeError(mode + " expected 1040 complete frames")
    if not all(all(x[k] == y[k] for k in keys) for x, y in zip(rows, reference)):
        raise RuntimeError(mode + " trajectory differs from reference")
    subset = rows[1002:1038]
    fields = [k for k in subset[0] if k.endswith("_ms")]
    out = {k: dict(mean=float(np.mean([float(r[k]) for r in subset])),
                   p95=float(np.percentile([float(r[k]) for r in subset], 95))) for k in fields}
    out["poses_identical"] = True
    events = read(a.results / (mode + "-trace.csv"))
    consume = [e for e in events if e["kind"] == "consume_wait"]
    starts = []
    for r in rows[1002:1039]:
        t = int(r["timestamp_ns"]) * 1e-9
        e = min(consume, key=lambda x: abs(float(x["frame_stamp"]) - t))
        if abs(float(e["frame_stamp"]) - t) > 1e-5:
            raise RuntimeError("Missing consume trace")
        starts.append(float(e["begin_ms"]))
    period = np.diff(starts)
    out["frame_period_ms"] = dict(mean=float(period.mean()), p95=float(np.percentile(period, 95)))
    out["fps"] = float(1000 / period.mean())
    out["frontend_active_ms"] = float(np.mean([float(r["front_total_ms"]) - float(r.get("front_snapshot_wait_ms", 0)) for r in subset]))
    report[mode] = out
(a.results / "pipeline-summary.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))

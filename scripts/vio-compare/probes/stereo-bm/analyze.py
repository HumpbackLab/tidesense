#!/usr/bin/env python3
"""Summarize board measurements; the first 20 frames are warmup."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
a = p.parse_args()
results = {}
for path in sorted((a.directory / "results").glob("*.csv")):
    rows = list(csv.DictReader(path.open()))
    data = rows[20:]
    summary = {"frames": len(rows), "measured_frames": len(data), "warmup_frames": 20}
    for key in ["read_ms", "rectify_ms", "bm_ms", "depth_ms", "process_ms", "with_read_ms", "valid_fraction"]:
        v = np.array([float(r[key]) for r in data])
        summary[key] = {"mean": float(v.mean()), "p50": float(np.median(v)),
                        "p95": float(np.percentile(v, 95)), "max": float(v.max())}
    summary["compute_fps"] = 1000 / summary["process_ms"]["mean"]
    summary["with_pgm_read_fps"] = 1000 / summary["with_read_ms"]["mean"]
    summary["fraction_processing_under_50ms"] = float(np.mean([float(r["process_ms"]) <= 50 for r in data]))
    results[path.stem] = summary
    print(f'{path.stem:28} BM {summary["bm_ms"]["mean"]:7.3f} ms '
          f'rect {summary["rectify_ms"]["mean"]:6.3f} ms depth {summary["depth_ms"]["mean"]:6.3f} ms '
          f'total {summary["process_ms"]["mean"]:7.3f} ms p95 {summary["process_ms"]["p95"]:7.3f} ms '
          f'FPS {summary["compute_fps"]:6.2f} I/O-FPS {summary["with_pgm_read_fps"]:6.2f} '
          f'valid {summary["valid_fraction"]["mean"]*100:5.1f}%')
(a.directory / "results.json").write_text(json.dumps(results, indent=2) + "\n")

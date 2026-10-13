#!/usr/bin/env python3
"""Compare isolated A53 variants and raw-disparity hashes."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
p.add_argument("--results-subdir", default="results")
p.add_argument("--output", default="results.json")
a = p.parse_args()
directory = a.directory / a.results_subdir
results = {}
for size in ["320x200", "640x400"]:
    paths = sorted(directory.glob(f"{size}-*-timing.csv"))
    basefile = directory / f"{size}-base-hashes.csv"
    if not basefile.exists():
        continue
    hashes = basefile.read_bytes()
    runs = {}
    for path in paths:
        name = path.name[len(size)+1:-len("-timing.csv")]
        rows = list(csv.DictReader(path.open()))
        data = rows[20:]
        if not data:
            continue
        stats = {"calls": len(rows), "measured_calls": len(data),
                 "hash_match": (path.parent / f"{size}-{name}-hashes.csv").read_bytes() == hashes}
        for key in ["bm_ms", "rectify_ms", "depth_ms", "process_ms", "valid_fraction"]:
            v = np.array([float(r[key]) for r in data])
            stats[key] = {"mean": float(v.mean()), "median": float(np.median(v)),
                          "p95": float(np.percentile(v, 95))}
        stats["compute_fps"] = 1000 / stats["process_ms"]["mean"]
        runs[name] = stats
    bases = [runs[k] for k in ["base", "base-repeat"] if k in runs]
    bm = sum(r["bm_ms"]["mean"] for r in bases) / len(bases)
    total = sum(r["process_ms"]["mean"] for r in bases) / len(bases)
    results[size] = {"baseline_bm_ms": bm, "baseline_process_ms": total, "runs": runs}
    print(size, f"baseline BM={bm:.3f} total={total:.3f} ms")
    for name, s in sorted(runs.items(), key=lambda x: x[1]["bm_ms"]["mean"]):
        s["bm_reduction_percent"] = 100*(1-s["bm_ms"]["mean"]/bm)
        s["process_reduction_percent"] = 100*(1-s["process_ms"]["mean"]/total)
        print(f'{name:16} BM={s["bm_ms"]["mean"]:8.3f} ms '
              f'total={s["process_ms"]["mean"]:8.3f} ms '
              f'BM reduction={s["bm_reduction_percent"]:6.2f}% '
              f'FPS={s["compute_fps"]:6.2f} hash_match={s["hash_match"]}')
(a.directory / a.output).write_text(json.dumps(results, indent=2) + "\n")

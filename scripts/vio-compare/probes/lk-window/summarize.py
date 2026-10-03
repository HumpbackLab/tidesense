#!/usr/bin/env python3
"""Summarize two full A53 replays per window plus fixed-input LK trials."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--gt", type=Path, required=True)
args = parser.parse_args()
windows = ("15x15", "16x15", "16x16")
variants = [f"{w}-r{r}" for r in (1, 2) for w in windows]
subprocess.run([sys.executable, str(here.parent / "backend-f32/compare.py"),
                str(args.root), "--variants", *variants, "--gt", str(args.gt),
                "--shift-ns", "-16684572"], check=True, stdout=subprocess.DEVNULL)
comparison = json.loads((args.root / "comparison.json").read_text())
result = {"gt_common_frames": comparison["gt_common_frames"],
          "common_initialized_frames": comparison["common_initialized_frames"], "windows": {}}
for window in windows:
    stats = [comparison["variants"][f"{window}-r{r}"] for r in (1, 2)]
    assert all(s["completed_run"] and s["frames"] == 1970 for s in stats)
    with (args.root / f"trajectory-{window}-r1.csv").open() as f:
        a = list(csv.DictReader(f))
    with (args.root / f"trajectory-{window}-r2.csv").open() as f:
        b = list(csv.DictReader(f))
    assert len(a) == len(b)
    for ra, rb in zip(a, b):
        assert {k: v for k, v in ra.items() if k != "compute_ms"} == {
            k: v for k, v in rb.items() if k != "compute_ms"}
    result["windows"][window] = {
        "frames": [s["frames"] for s in stats],
        "initialized": [s["initialized"] for s in stats],
        "repeat_trajectory_exact": True,
        "camera_all_ms": [s["camera_all_ms"]["mean"] for s in stats],
        "camera_common_initialized_ms": [s["camera_common_initialized_ms"]["mean"] for s in stats],
        "tracking_ms": [s["stages_ms"]["tracking"]["mean"] for s in stats],
        "backend_ms": [s["backend_ms"]["mean"] for s in stats],
        "ate_rmse_m": [s["ate_rmse_m"] for s in stats],
        "final_position_vs_15x15_m": [s["vs_reference_position_m"]["final"] for s in stats],
        "final_attitude_vs_15x15_deg": [s["vs_reference_attitude_deg"]["final"] for s in stats],
    }
with (args.root / "lk-pairs.csv").open() as f:
    pairs = list(csv.DictReader(f))
for w in windows:
    rows = [r for r in pairs if r["window"] == w]
    result["windows"][w]["fixed_lk"] = {
        "calls": len(rows),
        "mean_ms": float(np.mean([float(r["compute_ms"]) for r in rows])),
        "median_ms": float(np.median([float(r["compute_ms"]) for r in rows])),
        "valid_fraction": sum(int(r["valid"]) for r in rows) / sum(int(r["points"]) for r in rows),
    }
reference = result["windows"]["15x15"]
for w, data in result["windows"].items():
    data["camera_time_reduction_pct"] = 100 * (1 - np.mean(data["camera_all_ms"]) / np.mean(reference["camera_all_ms"]))
    data["tracking_time_reduction_pct"] = 100 * (1 - np.mean(data["tracking_ms"]) / np.mean(reference["tracking_ms"]))
    data["fixed_lk_time_reduction_pct"] = 100 * (1 - data["fixed_lk"]["mean_ms"] / reference["fixed_lk"]["mean_ms"])
(args.root / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))

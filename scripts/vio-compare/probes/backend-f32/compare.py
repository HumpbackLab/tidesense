#!/usr/bin/env python3
"""Compare throughput, numerical diagnostics and poses on common frames."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def summary(values):
    return {"mean": float(np.mean(values)), "p95": float(np.percentile(values, 95)),
            "max": float(np.max(values))} if len(values) else None


def load(root, name):
    exit_path = root / f"exit-{name}.txt"
    exit_code = int(exit_path.read_text().strip()) if exit_path.exists() else None
    with (root / f"trajectory-{name}.csv").open() as file:
        raw_rows = list(csv.DictReader(file))
    rows = []
    for index, row in enumerate(raw_rows):
        try:
            int(row["timestamp_ns"])
            float(row["compute_ms"])
            if row["initialized"] not in {"0", "1"}:
                raise ValueError("invalid initialized flag")
            if row["initialized"] == "1":
                for key in ("px", "py", "pz", "qx", "qy", "qz", "qw"):
                    float(row[key])
            for key in ("cov_dim", "cov_min_diag", "cov_asym_max"):
                if key in row:
                    float(row[key])
        except (KeyError, ValueError, TypeError):
            if exit_code not in (None, 0) and index == len(raw_rows) - 1:
                continue  # std::exit can leave the final buffered row truncated.
            raise RuntimeError(f"invalid trajectory row {index + 2} in {name}")
        rows.append(row)
    poses, initialized_ms, covariance, eigenvalues = {}, [], [], []
    for row in rows:
        if row["initialized"] != "1":
            continue
        pose = np.array([float(row[k]) for k in ("px", "py", "pz", "qx", "qy", "qz", "qw")])
        if not np.isfinite(pose).all():
            raise RuntimeError(f"nonfinite pose in {name}")
        poses[int(row["timestamp_ns"])] = pose
        initialized_ms.append(float(row["compute_ms"]))
        if row.get("cov_min_diag"):
            covariance.append([float(row["cov_min_diag"]), float(row["cov_asym_max"])])
        if row.get("cov_min_eigen"):
            eigenvalues.append(float(row["cov_min_eigen"]))
    stages = []
    stage_path = root / f"stages-{name}.csv"
    if stage_path.exists():
        with stage_path.open() as file:
            header = next(file).strip().lstrip("# ").split(",")
            stages = [[float(x) * 1000 for x in row[1:]] for row in csv.reader(file) if row]
    result = {"exit_code": exit_code, "completed_run": exit_code == 0 if exit_code is not None else None,
              "timing_scope": "completed CSV rows only" if exit_code not in (None, 0) else "full replay",
              "frames": len(rows), "discarded_truncated_rows": len(raw_rows) - len(rows), "initialized": len(poses),
              "camera_all_ms": summary([float(row["compute_ms"]) for row in rows]),
              "camera_initialized_ms": summary(initialized_ms)}
    if covariance:
        cov = np.array(covariance)
        result.update(covariance_min_diagonal=float(cov[:, 0].min()),
                      covariance_max_asymmetry=float(cov[:, 1].max()))
    if eigenvalues:
        result["covariance_min_sampled_eigenvalue"] = min(eigenvalues)
    if stages:
        stages = np.array(stages)
        result["stages_ms"] = {label.strip(): summary(stages[:, i]) for i, label in enumerate(header[1:])}
        result["backend_ms"] = summary(stages[:, 1:-1].sum(axis=1))
        result["stage_frames"] = len(stages)
    times = {int(row["timestamp_ns"]): float(row["compute_ms"]) for row in rows}
    return poses, result, times


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--variants", nargs="+", default=["original", "mixed", "fp32"])
    parser.add_argument("--gt", type=Path)
    parser.add_argument("--shift-ns", type=int, default=0)
    args = parser.parse_args()
    loaded = {name: load(args.root, name) for name in args.variants}
    common = sorted(set.intersection(*(set(poses) for poses, _, _ in loaded.values())))
    if not common:
        raise RuntimeError("No common initialized frames")
    baseline = np.array([loaded[args.variants[0]][0][t] for t in common])
    report = {"common_initialized_frames": len(common), "common_first_timestamp_ns": common[0],
              "common_last_timestamp_ns": common[-1], "variants": {}}
    for name, (poses, stats, times) in loaded.items():
        stats["camera_common_initialized_ms"] = summary([times[t] for t in common])
        estimate = np.array([poses[t] for t in common])
        error = np.linalg.norm(estimate[:, :3] - baseline[:, :3], axis=1)
        qa = estimate[:, 3:] / np.linalg.norm(estimate[:, 3:], axis=1)[:, None]
        qb = baseline[:, 3:] / np.linalg.norm(baseline[:, 3:], axis=1)[:, None]
        angle = np.degrees(2 * np.arccos(np.clip(np.abs(np.sum(qa * qb, axis=1)), 0, 1)))
        stats["vs_reference_position_m"] = {**summary(error), "rms": float(np.sqrt(np.mean(error**2))), "final": float(error[-1])}
        stats["vs_reference_attitude_deg"] = {**summary(angle), "final": float(angle[-1])}
        report["variants"][name] = stats
    if args.gt:
        gt = np.loadtxt(args.gt, delimiter=",", comments="#")
        selected = [t for t in common if gt[0, 0] <= t + args.shift_ns <= gt[-1, 0]]
        if not selected:
            raise RuntimeError("No GT-covered common frames")
        truth = np.column_stack([np.interp(np.array(selected) + args.shift_ns, gt[:, 0], gt[:, k]) for k in (1, 2, 3)])
        report["gt_common_frames"] = len(selected)
        for name, (poses, _, _) in loaded.items():
            estimate = np.array([poses[t][:3] for t in selected])
            x, y = estimate.mean(0), truth.mean(0)
            u, _, vt = np.linalg.svd((estimate - x).T @ (truth - y))
            correction = np.eye(3)
            correction[-1, -1] = np.linalg.det(u @ vt)
            aligned = (estimate - x) @ (u @ correction @ vt) + y
            error = np.linalg.norm(aligned - truth, axis=1)
            report["variants"][name]["ate_rmse_m"] = float(np.sqrt(np.mean(error**2)))
    (args.root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

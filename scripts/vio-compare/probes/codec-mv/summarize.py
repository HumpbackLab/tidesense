#!/usr/bin/env python3
"""Summarize complete runs; FPV position ATE uses SE(3), never fitted scale."""
import csv
import json
from pathlib import Path
import sys

# Ubuntu system NumPy, independent of the SDK's ROS Python environment.
sys.path.insert(0, '/usr/lib/python3/dist-packages')
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
WORK = ROOT / 'install/openvins-codec-mv'
POS = ['px', 'py', 'pz']
POSE = POS + ['qx', 'qy', 'qz', 'qw']

def read(path):
    return list(csv.DictReader(path.open()))

def array(rows, keys):
    return np.array([[float(r[k]) for k in keys] for r in rows])

def stats(a):
    return dict(mean=float(np.mean(a)), p50=float(np.median(a)),
                p95=float(np.percentile(a, 95)), max=float(np.max(a)))

def align(p, q):
    pc, qc = p.mean(axis=0), q.mean(axis=0)
    u, _, vt = np.linalg.svd((p - pc).T @ (q - qc))
    correction = np.eye(3)
    correction[2, 2] = np.linalg.det(u @ vt)
    rotation = u @ correction @ vt
    return (p - pc) @ rotation + qc

def main():
    files = sorted(f for f in (WORK / 'results').glob('*-poses.csv')
                   if f.with_name(f.name.removesuffix('-poses.csv') + '.json').exists())
    poses = {f.name.removesuffix('-poses.csv'): read(f) for f in files}
    gt = np.loadtxt(ROOT / 'install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv', delimiter=',')
    # Use a shared set of camera timestamps across every FPV variant, in GT range.
    fpv = [r for tag, r in poses.items() if tag.startswith('fpv-')]
    common = set.intersection(*[{int(r['timestamp_ns']) for r in rows if r['initialized'] == '1'} for rows in fpv]) if fpv else set()
    shift_ns = -16684572
    common = sorted(t for t in common if gt[0, 0] <= t + shift_ns <= gt[-1, 0])
    target_t = np.array(common, dtype=np.float64) + shift_ns
    q = np.column_stack([np.interp(target_t, gt[:, 0], gt[:, c]) for c in [1, 2, 3]])
    result = {}
    for tag, rows in poses.items():
        initialized = [r for r in rows if r['initialized'] == '1']
        assert initialized, tag
        p = array(initialized, POSE)
        timing = array(initialized[1:], ['compute_ms', 'mv_prepare_ms'])
        entry = dict(frames=len(rows), initialized=len(initialized), finite=bool(np.isfinite(p).all()),
                     endpoint_m=float(np.linalg.norm(p[-1, :3] - p[0, :3])),
                     max_displacement_m=float(np.linalg.norm(p[:, :3] - p[0, :3], axis=1).max()),
                     camera_ms=stats(timing[:, 0]), mv_prepare_ms=stats(timing[:, 1]))
        name, cap, mode, rep = tag.split('-')
        history = ROOT / f'install/openvins-mono-front/host-results/{name}-{"base" if cap == "r1000" else "r200"}-poses.csv'
        if mode == 'off' and history.exists():
            old = read(history)
            entry['historical_timestamps_identical'] = [r['timestamp_ns'] for r in rows] == [r['timestamp_ns'] for r in old]
            entry['historical_initialized_identical'] = [r['initialized'] for r in rows] == [r['initialized'] for r in old]
            old_init = [r for r in old if r['initialized'] == '1']
            entry['historical_pose_max_abs_diff'] = float(np.max(np.abs(p - array(old_init, POSE)))) if len(old_init) == len(p) else None
        detail = read(WORK / f'results/{tag}-detail.csv')
        d = array(detail, ['points', 'seeded', 'lk_valid', 'geometry_valid', 'seed_ms', 'lk_ms', 'normalize_ms', 'ransac_ms'])
        entry['detail'] = dict(calls=len(detail), mean_input_points=float(d[:, 0].mean()),
            seeded_fraction=float(d[:, 1].sum() / d[:, 0].sum()),
            lk_valid_fraction=float(d[:, 2].sum() / d[:, 0].sum()),
            mean_geometry_points=float(d[:, 3].mean()), below20_geometry_calls=int((d[:, 3] < 20).sum()),
            seed_ms_per_frame=float(d[:, 4].sum() / len(rows)),
            lk_ms_per_frame=float(d[:, 5].sum() / len(rows)),
            normalize_ms_per_frame=float(d[:, 6].sum() / len(rows)),
            ransac_ms_per_frame=float(d[:, 7].sum() / len(rows)))
        entry['detail']['lk_us_per_input_point'] = float(1000 * d[:, 5].sum() / d[:, 0].sum())
        seeded = [r for r in detail if int(r['seeded_geometry_valid']) > 0]
        if seeded:
            errors = array(seeded, ['seed_error_median_px', 'zero_error_median_px', 'seed_error_p95_px', 'zero_error_p95_px'])
            entry['proposal_diagnostics'] = dict(calls=len(seeded),
                median_of_frame_medians_seed_px=float(np.median(errors[:, 0])),
                median_of_frame_medians_zero_px=float(np.median(errors[:, 1])),
                mean_of_frame_p95_seed_px=float(np.mean(errors[:, 2])),
                mean_of_frame_p95_zero_px=float(np.mean(errors[:, 3])),
                warning='Distances to accepted LK output, conditional on LK+geometry success; not ground truth')
        if name == 'fpv':
            lookup = {int(r['timestamp_ns']): r for r in initialized}
            p_common = array([lookup[t] for t in common], POS)
            aligned = align(p_common, q)
            error = np.linalg.norm(aligned - q, axis=1)
            entry['ate'] = dict(rmse_m=float(np.sqrt(np.mean(error ** 2))), p95_m=float(np.percentile(error, 95)),
                                max_m=float(error.max()), samples=len(common),
                                duration_s=float((common[-1] - common[0]) * 1e-9), alignment='SE3, fixed scale')
        result[tag] = entry
    (WORK / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
    aggregate = {}
    for group in sorted({tag.rsplit('-', 1)[0] for tag in result}):
        runs = [r for tag, r in result.items() if tag.rsplit('-', 1)[0] == group]
        aggregate[group] = dict(repeats=len(runs),
            camera_ms_median_run_mean=float(np.median([r['camera_ms']['mean'] for r in runs])),
            camera_ms_run_mean_range=[min(r['camera_ms']['mean'] for r in runs), max(r['camera_ms']['mean'] for r in runs)],
            lk_ms_median_run_mean=float(np.median([r['detail']['lk_ms_per_frame'] for r in runs])),
            seed_ms_median_run_mean=float(np.median([r['detail']['seed_ms_per_frame'] for r in runs])),
            mv_prepare_ms_median_run_mean=float(np.median([r['mv_prepare_ms']['mean'] for r in runs])),
            ransac_ms_median_run_mean=float(np.median([r['detail']['ransac_ms_per_frame'] for r in runs])),
            endpoint_m=runs[0]['endpoint_m'], ate=runs[0].get('ate'))
        variants = [rows for tag, rows in poses.items() if tag.rsplit('-', 1)[0] == group]
        reference = [[r[k] for k in POSE] for r in variants[0] if r['initialized'] == '1']
        aggregate[group]['repeated_poses_identical'] = all(
            reference == [[r[k] for k in POSE] for r in rows if r['initialized'] == '1']
            for rows in variants)
    (WORK / 'aggregate.json').write_text(json.dumps(aggregate, indent=2) + '\n')
    print('run,endpoint_m,ATE_m,camera_ms,LK_ms,RANSAC_ms,geometry_points,below20,seed_pct')
    for tag, r in result.items():
        d = r['detail']
        print(f"{tag},{r['endpoint_m']:.4f},{r.get('ate', {}).get('rmse_m', float('nan')):.4f},"
              f"{r['camera_ms']['mean']:.3f},{d['lk_ms_per_frame']:.3f},{d['ransac_ms_per_frame']:.3f},"
              f"{d['mean_geometry_points']:.1f},{d['below20_geometry_calls']},{100*d['seeded_fraction']:.1f}")

if __name__ == '__main__':
    main()

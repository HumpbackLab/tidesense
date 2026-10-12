#!/usr/bin/env python3
"""Compare opt-in gyro frontend modes using identical FP64 replay settings."""
import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import numpy as np

repo = Path(__file__).resolve().parents[4]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
args = parser.parse_args()
work = args.work.resolve()
binary = work / 'run_record_gyro'
datasets = {
    'aggressive': repo / 'install/replay-20261012-003648',
    'previous': repo / 'install/replay-20261011-213326',
}
results = {}
for label, source in datasets.items():
    config = source / ('host-diagnosis/config/estimator_config.yaml' if label == 'aggressive'
                       else 'host-diagnosis/equi-full/config/estimator_config.yaml')
    record = source / 'record'
    first = int(next(csv.DictReader((record / 'stereo.csv').open()))['#timestamp_ns'])
    results[label] = {}
    for mode in ['off', 'initial', 'warp']:
        out = work / (label + '-' + mode)
        out.mkdir()
        env = os.environ.copy()
        for name in ['OPENVINS_FEATURE_GATE', 'OPENVINS_FEATURES', 'OPENVINS_STATES', 'OPENVINS_RESET_SLAM_FAILURES']:
            env.pop(name, None)
        env.update(OPENVINS_GYRO_MODE=mode, OPENVINS_GYRO_PROFILE='1',
                   OPENVINS_EKF_MA_MODE='original', OPENVINS_NUMERIC_EIGEN='1',
                   OPENVINS_FRONT_DIAG='1', OPENVINS_DIAG=str(out / 'backend.csv'))
        command = [str(binary), str(config), str(record), str(out / 'poses.csv'), '0']
        (out / 'command.json').write_text(json.dumps({'command': command, 'mode': mode,
              'calibration': str(config), 'feature_budget': 150, 'precision': 'FP64'}, indent=2) + '\n')
        print(label, mode, 'started', flush=True)
        with (out / 'run.log').open('w') as log:
            run = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        (out / 'exit').write_text(str(run.returncode) + '\n')
        poses = list(csv.DictReader((out / 'poses.csv').open()))
        valid = [r for r in poses if r['initialized'] == '1']
        result = {'exit': run.returncode, 'frames': len(poses), 'poses': len(valid)}
        if valid:
            a = np.array([[float(r[k]) for k in ['timestamp_ns', 'px', 'py', 'pz', 'compute_ms']] for r in valid])
            displacement = np.linalg.norm(a[:, 1:4] - a[0, 1:4], axis=1)
            result.update(endpoint_m=float(displacement[-1]), max_displacement_m=float(displacement.max()),
                          max_frame_step_m=float(np.linalg.norm(np.diff(a[:, 1:4], axis=0), axis=1).max()),
                          mean_compute_ms=float(a[:, 4].mean()), p95_compute_ms=float(np.percentile(a[:, 4], 95)),
                          first_valid_s=float((a[0, 0] - first) * 1e-9))
        rows = {}
        for r in csv.reader((out / 'backend.csv').open()):
            rows.setdefault(r[0], []).append([float(x) for x in r[1:]])
        rows = {k: np.array(v) for k, v in rows.items()}
        windows = []
        for lo, hi in [(15, 17), (17, 19), (19, 25), (25, 42)]:
            if label != 'aggressive': continue
            d = {'start_s': lo, 'end_s': hi}
            if 'TRACK' in rows:
                a = rows['TRACK']; q = a[(a[:, 0] - first * 1e-9 >= lo) & (a[:, 0] - first * 1e-9 < hi)]
                if len(q): d['median_retained_left_right_shared'] = np.median(q[:, 3:6], axis=0).tolist()
            if 'MSCKF' in rows:
                a = rows['MSCKF']; q = a[(a[:, 0] - first * 1e-9 >= lo) & (a[:, 0] - first * 1e-9 < hi)]
                if len(q):
                    v = q[:, 1:7].sum(axis=0)
                    d['msckf_input_short_tri_test_reject_accept'] = v.tolist()
                    d['chi2_reject_fraction'] = float(v[4] / v[3]) if v[3] else None
            if 'UPDATE' in rows:
                a = rows['UPDATE']; q = a[(a[:, 0] - first * 1e-9 >= lo) & (a[:, 0] - first * 1e-9 < hi)]
                if len(q): d['no_visual_update_fraction'] = float(np.mean(q[:, 1:4].sum(axis=1) == 0))
            windows.append(d)
        result['windows'] = windows
        results[label][mode] = result
        (out / 'summary.json').write_text(json.dumps(result, indent=2) + '\n')
        (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(label, mode, 'finished', result.get('endpoint_m'), 'm', result.get('mean_compute_ms'), 'ms', flush=True)

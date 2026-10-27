#!/usr/bin/env python3
"""Score RV1103 ov-SchurVINS and the OpenVINS controls without fitting scale."""
import argparse
import csv
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('work', type=Path)
parser.add_argument('--evaluator', type=Path, default=Path('/home/ncer/fpga_openvins/benchmarks/ov_schurvins_replay/evaluate_trajectory.py'))
args = parser.parse_args()
scripts = Path(__file__).resolve().parent
repo = scripts.parents[3]
work = args.work.resolve()
results = work / 'results'
gt = repo / 'install/rv1103-openvins-20261025/dataset/mav0/state_groundtruth_estimate0/data.csv'
spec = importlib.util.spec_from_file_location('trajectory_evaluator', args.evaluator)
evaluator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluator)
trials = {}
for path in sorted(results.glob('*.json')):
    trial = json.loads(path.read_text())
    if trial.get('benchmark') not in ['ov-schurvins-replay', 'openvins-full-replay']:
        continue
    schur = trial['benchmark'] == 'ov-schurvins-replay'
    name = path.stem
    if trial['samples'] != 400 or trial['initialization'] != 'native':
        raise ValueError(f'Incomplete or non-native trial: {name}')
    poses = results / f'{name}-poses.csv'
    values = np.loadtxt(poses, delimiter=',', comments='#')
    if values.shape[0] != 405 or np.any(values[:, 12] < 0):
        raise ValueError(f'Unexpected frame count or negative covariance: {name}')
    if schur:
        if (trial['fej'] or trial['online_calibration'] or trial['schur_updates'] <= 0
            or np.any(np.diff(values[:, 14]) < 0)
            or values[-1, 14] != trial['schur_updates']):
            raise ValueError(f'Invalid Schur update counters/options: {name}')
    ate = evaluator.evaluate(poses, gt)
    (results / f'{name}-ate.json').write_text(json.dumps(ate, indent=2)+'\n')
    command = [sys.executable, str(scripts / 'analyze.py'),
        str(results / f'{name}-detail.csv'), '--stages', str(results / f'{name}-stages.csv'),
        '--update-name', 'schur' if schur else 'msckf']
    samples = results / f'{name}-samples.txt'
    if samples.exists():
        command += ['--perf-script', str(samples)]
    summary = json.loads(subprocess.check_output(command, text=True))
    (results / f'{name}-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    trials[name] = dict(benchmark=trial, ate=ate, detail=summary,
        first_camera_timestamp_ns=int(values[0, 0]),
        last_camera_timestamp_ns=int(values[-1, 0]),
        covariance_min_diagonal=float(values[:, 12].min()),
        covariance_max_asymmetry=float(values[:, 13].max()))
    numeric = results / f'{name}-numeric.csv'
    if numeric.exists():
        with numeric.open() as file:
            rows = list(csv.DictReader(file))
        if len(rows) != len(values):
            raise ValueError(f'Numeric diagnostic coverage mismatch: {name}')
        eigenvalues = [float(row['min_eigenvalue_sampled']) for row in rows
                       if row['min_eigenvalue_sampled']]
        if not eigenvalues or not np.isfinite(eigenvalues).all():
            raise ValueError(f'Invalid covariance eigenvalue diagnostics: {name}')
        trials[name]['numeric_diagnostics'] = dict(frames=len(rows),
            eigenvalue_samples=len(eigenvalues), minimum_eigenvalue=min(eigenvalues),
            negative_eigenvalue_samples=sum(value < 0 for value in eigenvalues))
(work / 'schur-results.json').write_text(json.dumps(trials, indent=2)+'\n')
for name, trial in trials.items():
    b = trial['benchmark']
    print(f"{name}: VIO={b['vio_mean_ms']:.3f} ms, backend={b['backend_mean_ms']:.3f} ms, "
          f"ATE={trial['ate']['position_ate_rmse_m']*100:.3f} cm")

#!/usr/bin/env python3
"""Score all complete backend trials against EuRoC and the same RKIVE FP64 run."""
import argparse
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
work = args.work.resolve()
scripts = Path(__file__).resolve().parent
repo = scripts.parents[3]
gt = repo / 'install/rv1103-openvins-20261025/dataset/mav0/state_groundtruth_estimate0/data.csv'
spec = importlib.util.spec_from_file_location('trajectory_evaluator', args.evaluator)
evaluator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluator)
results = work / 'results'
baseline = np.loadtxt(results / 'fp64-reference-400-poses.csv', delimiter=',', comments='#')
trials = {}
for path in sorted(results.glob('*.json')):
    trial = json.loads(path.read_text())
    if trial.get('benchmark') != 'openvins-full-replay':
        continue
    name = path.stem
    if trial['samples'] != 400 or trial['frames'] != 495 or not trial['outlier_gate']:
        raise ValueError(f'Incomplete or unexpected run: {name}')
    poses = results / f'{name}-poses.csv'
    values = np.loadtxt(poses, delimiter=',', comments='#')
    if values.shape != baseline.shape or not np.array_equal(values[:, 0], baseline[:, 0]):
        raise ValueError(f'Trajectory frame coverage mismatch: {name}')
    ate = evaluator.evaluate(poses, gt)
    (results / f'{name}-ate.json').write_text(json.dumps(ate, indent=2) + '\n')
    summary = subprocess.check_output([sys.executable, str(scripts / 'analyze.py'),
        str(results / f'{name}-detail.csv'), '--stages', str(results / f'{name}-stages.csv')], text=True)
    (results / f'{name}-summary.json').write_text(summary)
    difference = np.linalg.norm(values[:, 6:9] - baseline[:, 6:9], axis=1)
    quat_dot = np.abs(np.sum(values[:, 2:6] * baseline[:, 2:6], axis=1))
    angle = 2 * np.arccos(np.clip(quat_dot, 0, 1)) * 180 / np.pi
    trials[name] = dict(benchmark=trial, ate=ate, detail=json.loads(summary),
        covariance_min_diagonal=float(values[:, 12].min()),
        covariance_max_asymmetry=float(values[:, 13].max()),
        difference_from_fp64=dict(position_rms_m=float(np.sqrt(np.mean(difference**2))),
            position_final_m=float(difference[-1]), attitude_final_deg=float(angle[-1])))
(work / 'backend-results.json').write_text(json.dumps(trials, indent=2) + '\n')
for name, trial in trials.items():
    b = trial['benchmark']
    print(f"{name}: VIO={b['vio_mean_ms']:.3f} ms, backend={b['backend_mean_ms']:.3f} ms, "
          f"compression={trial['detail']['timing_ms']['msckf_compress_ms']['mean']:.3f} ms, "
          f"ATE={trial['ate']['position_ate_rmse_m']*100:.3f} cm")

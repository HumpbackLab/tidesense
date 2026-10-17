#!/usr/bin/env python3
"""Full-coverage native SchurVINS replay; match IMU noise without changing timestamps."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np
import yaml

VARIANTS = {
    'baseline': {},
    'stable': {'SCHUR_STABLE_UPDATE': '1'},
    'pixel-stable': {'SCHUR_STABLE_UPDATE': '1', 'SCHUR_PIXEL_RESIDUAL': '1'},
    'matched-imu': {},
    'matched-imu-stable': {'SCHUR_STABLE_UPDATE': '1'},
    'matched-imu-pixel-stable': {'SCHUR_STABLE_UPDATE': '1', 'SCHUR_PIXEL_RESIDUAL': '1'},
}
NOISE_KEYS = {
    'sigma_omega_c': 'gyroscope_noise_density',
    'sigma_acc_c': 'accelerometer_noise_density',
    'sigma_omega_bias_c': 'gyroscope_random_walk',
    'sigma_acc_bias_c': 'accelerometer_random_walk',
}


def rows(path):
    with path.open() as stream:
        return [row for row in csv.reader(stream) if row and not row[0].startswith('#')]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_yaml(path):
    return yaml.safe_load('\n'.join(line for line in path.read_text().splitlines()
                                    if not line.startswith('%YAML:')))


def evaluate(out, stereo, groundtruth):
    poses, gt = rows(out / 'poses.csv'), rows(groundtruth)
    stamps = [int(row[0]) for row in poses]
    if stamps != [int(row[0]) for row in stereo]:
        raise ValueError('Pose timestamps must cover every input pair in order; subset ATE is disallowed')
    values = np.array([[float(v) for v in row[1:8]] for row in poses])
    if values.shape != (len(stereo), 7) or not np.isfinite(values).all():
        raise ValueError('Nonfinite or malformed poses')
    if not np.allclose(np.linalg.norm(values[:, 3:7], axis=1), 1, atol=1e-3):
        raise ValueError('Invalid pose quaternion')
    origin = int(gt[0][0])
    t = np.array([(stamp - origin) * 1e-9 for stamp in stamps])
    tg = np.array([(int(row[0]) - origin) * 1e-9 for row in gt])
    if not np.all(np.diff(tg) > 0):
        raise ValueError('GT timestamps must be strictly increasing')
    gp = np.array([[float(v) for v in row[1:4]] for row in gt])
    valid = (t >= tg[0]) & (t <= tg[-1])
    if valid.sum() < 3:
        raise ValueError('Insufficient GT overlap')
    x = values[valid, :3]
    y = np.column_stack([np.interp(t[valid], tg, gp[:, i]) for i in range(3)])
    u, _, vt = np.linalg.svd((x - x.mean(0)).T @ (y - y.mean(0)))
    sign = np.eye(3)
    sign[-1, -1] = np.linalg.det(vt.T @ u.T)
    rotation = vt.T @ sign @ u.T
    error = np.linalg.norm((x - x.mean(0)) @ rotation.T + y.mean(0) - y, axis=1)
    observations = [int(v) for v in re.findall(r'nObs = (\d+)', (out / 'run.log').read_text())]
    result = {
        'pose_rows': len(poses), 'matched_pose_rows': int(valid.sum()),
        'full_pose_coverage': True, 'all_finite': True,
        'ate_se3_rmse_m': float(np.sqrt(np.mean(error**2))),
        'ate_p95_m': float(np.quantile(error, .95)),
        'backend_zero_observation_logs': observations.count(0),
        'alignment': 'SE3 rotation+translation, scale fixed 1; linear GT position interpolation; no fitted time offset',
    }
    timing = rows(out / 'timing.csv')
    if timing and timing[0][0] == 'frame':
        timing = timing[1:]
    if timing:
        ms = np.array([float(row[2]) for row in timing])
        result.update(compute_ms_mean=float(ms.mean()), compute_ms_p95=float(np.quantile(ms, .95)))
    (out / 'evaluation.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('binary', 'calibration', 'stereo', 'imu', 'groundtruth', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--matched-imu', type=Path, help='OpenVINS imu0 YAML; copy only four noise densities/random walks')
    parser.add_argument('--variants', nargs='+', choices=VARIANTS, default=['baseline', 'matched-imu'])
    parser.add_argument('--repeats', type=int, default=1)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    if any(name.startswith('matched-imu') for name in args.variants) and not args.matched_imu:
        parser.error('--matched-imu is required for matched-imu variants')
    for name in ('binary', 'calibration', 'stereo', 'imu', 'groundtruth'):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    stereo = rows(args.stereo)
    if not stereo or any(int(b[0]) <= int(a[0]) for a, b in zip(stereo, stereo[1:])):
        raise ValueError('Stereo timestamps must be strictly increasing')
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    calibration = args.calibration
    if args.matched_imu:
        config = read_yaml(args.calibration)
        imu_config = read_yaml(args.matched_imu)['imu0']
        for dst, src in NOISE_KEYS.items():
            value = float(imu_config[src])
            if not np.isfinite(value) or value <= 0:
                raise ValueError('IMU noise values must be finite and positive')
            config['imu_params'][dst] = value
        calibration = args.output / 'matched-imu.yaml'
        calibration.write_text(yaml.safe_dump(config, sort_keys=False))
    hashes = {name: sha256(getattr(args, name)) for name in
              ('binary', 'calibration', 'stereo', 'imu', 'groundtruth')}
    library = args.binary.parent / 'libschurvins.so'
    if library.exists():
        hashes['libschurvins.so'] = sha256(library)
    if args.matched_imu:
        hashes['matched_imu_source'] = sha256(args.matched_imu)
        hashes['matched_imu_calibration'] = sha256(calibration)
    summary = {'inputs': {name: str(getattr(args, name)) for name in
                         ('binary', 'calibration', 'stereo', 'imu', 'groundtruth')},
               'sha256': hashes, 'runs': {}}
    for name in args.variants:
        for repeat in range(args.repeats):
            key = name if repeat == 0 else f'{name}-repeat{repeat}'
            out = args.output / key
            out.mkdir()
            selected = calibration if name.startswith('matched-imu') else args.calibration
            env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
            settings = dict(SCHUR_MICRO_ROS='0', SCHUR_PREDICTION='0', SCHUR_TIMING_CSV=str(out / 'timing.csv'))
            settings.update(VARIANTS[name])
            env.update(settings)
            command = [str(args.binary), str(selected), str(args.stereo), str(args.imu), str(out / 'poses.csv')]
            (out / 'run.json').write_text(json.dumps({'command': command, 'environment': settings}, indent=2) + '\n')
            with (out / 'run.log').open('w') as log:
                subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            result = evaluate(out, stereo, args.groundtruth)
            result['poses_sha256'] = sha256(out / 'poses.csv')
            summary['runs'][key] = result
            (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(f'{key}: ATE={result["ate_se3_rmse_m"]:.6f} m, GT frames={result["matched_pose_rows"]}, '
                  f'zero observations={result["backend_zero_observation_logs"]}', flush=True)


if __name__ == '__main__':
    main()

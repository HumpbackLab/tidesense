#!/usr/bin/env python3
"""Replay a lossless TideSense recording with four backend ablations, without GT."""
import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

import numpy as np
import yaml

VARIANTS = {
    'baseline': {},
    'stable': {'SCHUR_STABLE_UPDATE': '1'},
    'pixel': {'SCHUR_PIXEL_RESIDUAL': '1'},
    'pixel-stable': {'SCHUR_STABLE_UPDATE': '1', 'SCHUR_PIXEL_RESIDUAL': '1'},
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def rows(path):
    with path.open() as stream:
        return [r for r in csv.reader(stream) if r and not r[0].startswith('#')]


def save(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def validate(source, recording):
    sdk = Path(__file__).resolve().parents[4]
    spec = importlib.util.spec_from_file_location('dataset_export', sdk / 'scripts/dataset/export.py')
    export = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(export)
    stereo = rows(recording / 'stereo.csv')
    first = next(export.frames(source))[0][5]
    offset = int(stereo[0][0]) - first
    result = export.validate(source, offset)
    count = 0
    for count, (header, left, right) in enumerate(export.frames(source), 1):
        row = stereo[count - 1]
        if int(row[0]) != header[5] + offset:
            raise ValueError('Export camera timestamp mismatch')
        for relative, pixels in zip(row[1:], (left, right)):
            if (recording / relative).read_bytes() != b'P5\n640 400\n255\n' + pixels:
                raise ValueError(f'Export image pixels mismatch: {relative}')
    if count != len(stereo):
        raise ValueError('Export pair count mismatch')
    imu = rows(recording / 'imu.csv')
    original = list(export.imu_rows(source))
    if len(imu) != len(original):
        raise ValueError('Export IMU count mismatch')
    for row, (stamp, values) in zip(imu, original):
        if int(row[0]) != stamp or [float(v) for v in row[1:]] != values[:6]:
            raise ValueError('Export IMU mismatch')
    result.update(lossless_pixels_verified=True, imu_values_verified=True,
                  archive_sha256={p.name: sha256(p) for p in sorted(source.iterdir()) if p.is_file()},
                  replay_sha256={name: sha256(recording / name)
                                 for name in ('replay.yaml', 'stereo.csv', 'imu.csv')})
    return result, stereo


def measure(out, stereo):
    poses = rows(out / 'poses.csv')
    stamps = [int(r[0]) for r in poses]
    expected = [int(r[0]) for r in stereo]
    if not stamps or any(b <= a for a, b in zip(stamps, stamps[1:])):
        raise ValueError('Empty or nonmonotonic poses')
    if not set(stamps).issubset(expected):
        raise ValueError('Unexpected pose timestamp')
    values = np.array([[float(v) for v in r[1:8]] for r in poses])
    if values.shape != (len(poses), 7) or not np.isfinite(values).all():
        raise ValueError('Nonfinite/malformed poses')
    if not np.allclose(np.linalg.norm(values[:, 3:], axis=1), 1, atol=1e-3):
        raise ValueError('Invalid quaternion')
    timing = rows(out / 'timing.csv')[1:]
    log = (out / 'run.log').read_text()
    completion = re.search(r'frames=(\d+) tracking=(\d+) imu_fused=(\d+)', log)
    if not completion or int(completion[1]) != len(stereo):
        raise ValueError('Replay did not consume the entire recording')
    obs = [int(v) for v in re.findall(r'nObs = (\d+)', log)]
    p = values[:, :3]
    t = (np.array(stamps, dtype=np.int64) - stamps[0]) * 1e-9
    steps = np.linalg.norm(np.diff(p, axis=0), axis=1)
    ms = np.array([float(r[2]) for r in timing])
    return dict(input_pairs=len(stereo), processed_frames=int(completion[1]), pose_rows=len(poses),
                full_pose_coverage=stamps == expected, all_finite=True,
                first_pose_s=(stamps[0] - expected[0]) * 1e-9,
                last_pose_s=(stamps[-1] - expected[0]) * 1e-9,
                endpoint_displacement_m=float(np.linalg.norm(p[-1] - p[0])),
                max_distance_from_first_pose_m=float(np.linalg.norm(p - p[0], axis=1).max()),
                estimated_path_m=float(steps.sum()),
                max_pose_step_m=float(steps.max()),
                max_finite_difference_speed_m_s=float((steps / np.diff(t)).max()),
                backend_zero_observation_logs=obs.count(0), backend_observation_logs=len(obs),
                backend_observations_median=float(np.median(obs)) if obs else None,
                compute_ms_mean=float(ms.mean()), compute_ms_p95=float(np.quantile(ms, .95)),
                poses_sha256=sha256(out / 'poses.csv'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'recording', 'binary', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--calibration', type=Path,
                        help='Use this exact YAML; stereo.csv must already include its camera time offset')
    parser.add_argument('--repeats', type=int, default=2)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    for name in ('source', 'recording', 'binary'):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    validation, stereo = validate(args.source, args.recording)
    calibration = (args.calibration or args.recording / 'replay.yaml').resolve(strict=True)
    config = yaml.safe_load('\n'.join(line for line in calibration.read_text().splitlines()
                                      if not line.startswith('%YAML:')))
    live_offset = int(config.get('live_camera_time_offset_ns', 0))
    if live_offset and live_offset != validation['camera_offset_ns']:
        raise ValueError('CSV camera time offset differs from the supplied calibration')
    if float(config['imu_params'].get('delay_imu_cam', 0)) != 0:
        raise ValueError('Replay requires delay_imu_cam=0 and aligned stereo timestamps')
    summary = dict(source=str(args.source), recording=str(args.recording), binary=str(args.binary),
                   validation=validation, binary_sha256=sha256(args.binary),
                   calibration=str(calibration), calibration_sha256=sha256(calibration),
                   camera_offset_applied_to_csv_ns=validation['camera_offset_ns'],
                   yaml_live_offset_ns=live_offset,
                   offline_live_offset_applied_again=False,
                   library_sha256=sha256(args.binary.parent / 'libschurvins.so'),
                   protocol='Host FP64; identical input, calibration and IMU noise; no GT; no parameter sweep.',
                   runs={})
    shutil.copy2(calibration, args.output / 'input-calibration.yaml')
    print(json.dumps(validation, indent=2), flush=True)
    save(args.output / 'summary.json', summary)
    for variant, switches in VARIANTS.items():
        out = args.output / variant
        out.mkdir()
        command = [str(args.binary), str(calibration),
                   str(args.recording / 'stereo.csv'), str(args.recording / 'imu.csv'), str(out / 'poses.csv')]
        settings = dict(SCHUR_MICRO_ROS='0', SCHUR_PREDICTION='0',
                        SCHUR_TIMING_CSV=str(out / 'timing.csv'), **switches)
        env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
        env.update(settings)
        save(out / 'run.json', dict(command=command, environment=settings))
        results = []
        for repeat in range(args.repeats):
            start = time.monotonic()
            with (out / 'run.log').open('w') as log:
                subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
            result = measure(out, stereo)
            result['wall_s'] = time.monotonic() - start
            results.append(result)
            copy = out / f'repeat-{repeat}'
            copy.mkdir()
            for name in ('poses.csv', 'timing.csv', 'run.log'):
                shutil.copy2(out / name, copy / name)
            print(f'{variant} repeat {repeat}: {json.dumps(result)}', flush=True)
        summary['runs'][variant] = dict(results=results,
                                       repeat_pose_identity=len({r['poses_sha256'] for r in results}) == 1)
        save(args.output / 'summary.json', summary)


if __name__ == '__main__':
    main()

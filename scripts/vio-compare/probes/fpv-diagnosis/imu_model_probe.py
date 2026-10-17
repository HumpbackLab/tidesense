#!/usr/bin/env python3
"""Diagnostic IMU-input and integration ablations; learned corrections are in-sample."""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess

import numpy as np

from recording_ab import measure, rows, save, sha256


def correction(row):
    def matrix(prefix):
        m = np.zeros((3, 3))
        for i, j in ((0, 0), (1, 0), (2, 0), (1, 1), (2, 1), (2, 2)):
            m[i, j] = float(row[f'{prefix}{i}{j}'])
        return m
    q = np.array([float(row[k]) for k in ('qx', 'qy', 'qz', 'qw')])
    q /= np.linalg.norm(q)
    v, w = q[:3], q[3]
    x, y, z = v
    skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    # OpenVINS quat_2_Rot uses JPL: minus the skew term.
    rotation = (2 * w * w - 1) * np.eye(3) - 2 * w * skew + 2 * np.outer(v, v)
    return matrix('dw'), matrix('da'), rotation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('binary', 'calibration', 'recording', 'intrinsics', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--variants', nargs='+', default=[
        'raw', 'gyro-diagonal', 'gyro-full', 'accel-full', 'both-full',
        'midpoint', 'both-full-midpoint'])
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--intrinsics-until-s', type=float,
                        help='Use the last parameter estimate before this recording-relative time')
    parser.add_argument('--intrinsics-origin-ns', type=int,
                        help='First camera timestamp of the calibration-source recording, for transfer tests')
    args = parser.parse_args()
    for key in ('binary', 'calibration', 'recording', 'intrinsics'):
        setattr(args, key, getattr(args, key).resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    stereo = rows(args.recording / 'stereo.csv')
    origin = args.intrinsics_origin_ns or int(stereo[0][0])
    with args.intrinsics.open() as stream:
        estimates = list(csv.DictReader(stream))
    if args.intrinsics_until_s is not None:
        cutoff = origin + int(args.intrinsics_until_s * 1e9)
        estimates = [row for row in estimates if int(row['timestamp_ns']) <= cutoff]
    if not estimates:
        raise ValueError('No calibration estimate before cutoff')
    last = estimates[-1]
    dw, da, rotation = correction(last)
    gyro = rotation @ dw
    eye = np.eye(3)
    variants = {
        'raw': (eye, eye, {}),
        'gyro-diagonal': (np.diag(dw.diagonal()), eye, {}),
        'gyro-axis': (dw, eye, {}),
        'gyro-rotation': (rotation, eye, {}),
        'static-gyro-bias': (eye, eye, {}),
        'gyro-full': (gyro, eye, {}),
        'accel-full': (eye, da, {}),
        'both-full': (gyro, da, {}),
        'midpoint': (eye, eye, {'SCHUR_IMU_MIDPOINT': '1'}),
        'both-full-midpoint': (gyro, da, {'SCHUR_IMU_MIDPOINT': '1'}),
        'gyro-full-midpoint': (gyro, eye, {'SCHUR_IMU_MIDPOINT': '1'}),
        'stable-midpoint': (eye, eye, {'SCHUR_IMU_MIDPOINT': '1', 'SCHUR_STABLE_UPDATE': '1'}),
    }
    raw = rows(args.recording / 'imu.csv')
    values = np.array([[float(v) for v in row[1:]] for row in raw])
    stationary = np.array([int(row[0]) - int(stereo[0][0]) for row in raw]) * 1e-9
    static_bias = values[(stationary >= 0) & (stationary < 10), :3].mean(axis=0)
    summary = dict(protocol='Host FP64, fixed camera calibration/noise/input timestamps. '
                   'Final rounded OpenVINS IMU correction from the SAME recording: diagnostic, not independent validation.',
                   binary=str(args.binary), binary_sha256=sha256(args.binary),
                   library_sha256=sha256(args.binary.parent / 'libschurvins.so'),
                   calibration=str(args.calibration), calibration_sha256=sha256(args.calibration),
                   imu_sha256=sha256(args.recording / 'imu.csv'),
                   intrinsics=str(args.intrinsics), intrinsics_sha256=sha256(args.intrinsics),
                   estimate_time_s=(int(last['timestamp_ns']) - origin) * 1e-9,
                   estimate_origin_ns=origin, estimate_timestamp_ns=int(last['timestamp_ns']),
                   dw=dw.tolist(), da=da.tolist(), gyro_to_imu_rotation=rotation.tolist(), runs={})
    for name in args.variants:
        g, a, settings = variants[name]
        out = args.output / name
        out.mkdir()
        imu = args.recording / 'imu.csv'
        bias = static_bias if name == 'static-gyro-bias' else np.zeros(3)
        if name == 'static-gyro-bias' or not (np.array_equal(g, eye) and np.array_equal(a, eye)):
            corrected = np.column_stack(((values[:, :3] - bias) @ g.T, values[:, 3:] @ a.T))
            imu = out / 'imu.csv'
            with imu.open('w') as stream:
                stream.write('#timestamp_ns,gx,gy,gz,ax,ay,az\n')
                for row, value in zip(raw, corrected):
                    stream.write(row[0] + ',' + ','.join(format(v, '.17g') for v in value) + '\n')
        env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
        runtime = dict(SCHUR_MICRO_ROS='0', SCHUR_PREDICTION='0',
                       SCHUR_TIMING_CSV=str(out / 'timing.csv'),
                       SCHUR_IMU_DEBUG_CSV=str(out / 'states.csv'), **settings)
        env.update(runtime)
        command = [str(args.binary), str(args.calibration), str(args.recording / 'stereo.csv'),
                   str(imu), str(out / 'poses.csv')]
        save(out / 'command.json', dict(command=command, environment=runtime,
                                       gyro_matrix=g.tolist(), accel_matrix=a.tolist(),
                                       subtracted_static_gyro_bias=bias.tolist(), imu_sha256=sha256(imu)))
        runs = []
        for repeat in range(args.repeats):
            with (out / 'run.log').open('w') as stream:
                subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=300)
            result = measure(out, stereo)
            runs.append(result)
            copy = out / f'repeat-{repeat}'
            copy.mkdir()
            for file in ('poses.csv', 'timing.csv', 'run.log', 'states.csv'):
                if (out / file).exists():
                    shutil.copy2(out / file, copy / file)
            print(name, repeat, result['endpoint_displacement_m'], flush=True)
        summary['runs'][name] = dict(results=runs, repeat_pose_identity=len({r['poses_sha256'] for r in runs}) == 1)
        save(args.output / 'summary.json', summary)


if __name__ == '__main__':
    main()

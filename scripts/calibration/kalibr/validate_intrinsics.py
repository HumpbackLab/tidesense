#!/usr/bin/env python3
"""Validate a Kalibr residual IMU calibration on a separate TideSense recording."""
import argparse
import copy
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np
import yaml

SDK = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(SDK / 'scripts/vio-compare/probes/fpv-diagnosis'))
from recording_ab import measure, rows, save, sha256


def load(path):
    return yaml.safe_load('\n'.join(v for v in path.read_text().splitlines() if not v.startswith('%YAML:')))


def matrices(document):
    imu = document['imu0']
    if imu['model'] != 'scale-misalignment':
        raise ValueError('Expected the Kalibr scale-misalignment measurement model')
    if not np.allclose(imu['T_i_b'], np.eye(4), atol=1e-8) or abs(imu.get('time_offset', 0)) > 1e-12:
        raise ValueError('Only the reference IMU with identity T_i_b is supported')
    ma = np.array(imu['accelerometers']['M'], dtype=float)
    mw = np.array(imu['gyroscopes']['M'], dtype=float)
    c = np.array(imu['gyroscopes']['C_gyro_i'], dtype=float)
    a = np.array(imu['gyroscopes']['A'], dtype=float)
    for m in (ma, mw, c, a):
        if m.shape != (3, 3) or not np.isfinite(m).all():
            raise ValueError('Malformed intrinsic matrix')
    if not np.allclose(c.T @ c, np.eye(3), atol=1e-6) or np.linalg.det(c) < .99:
        raise ValueError('C_gyro_i is not a rotation')
    if max(np.linalg.cond(ma), np.linalg.cond(mw)) > 1e4:
        raise ValueError('Singular intrinsic correction')
    return ma, mw, c, a


def correction_checks(ma, mw, c, a):
    # Kalibr predicts measurements, unlike the inverse D matrices in OpenVINS.
    # Include nonzero biases to verify the residual-bias coordinate change too.
    rng = np.random.default_rng(27)
    av, wv = rng.normal(size=(2, 80, 3))
    ba = np.array([.1, -.2, .3]); bg = np.array([-.01, .02, .03])
    am = av @ ma.T + ba
    wm = wv @ c.T @ mw.T + av @ c.T @ a.T + bg
    da, gw = np.linalg.inv(ma), c.T @ np.linalg.inv(mw)
    ae = am @ da.T
    we = (wm - ae @ c.T @ a.T) @ gw.T
    expected_ba = da @ ba
    expected_bg = gw @ (bg - a @ c @ expected_ba)
    if not np.allclose(ae, av + expected_ba, atol=1e-10) or not np.allclose(we, wv + expected_bg, atol=1e-10):
        raise ValueError('Forward/inverse model consistency check failed')


def camera_yaml(base, chain):
    config = copy.deepcopy(base)
    for i, entry in enumerate(config['cameras']):
        source = chain[f'cam{i}']
        if source['camera_model'] != 'pinhole' or source['distortion_model'] not in ('radtan', 'equidistant'):
            raise ValueError('Unsupported camera model')
        camera = entry['camera']
        camera['image_width'], camera['image_height'] = source['resolution']
        camera['intrinsics']['data'] = source['intrinsics']
        camera['distortion']['type'] = 'radial-tangential' if source['distortion_model'] == 'radtan' else 'equidistant'
        camera['distortion']['parameters']['data'] = source['distortion_coeffs']
        entry['T_B_C']['data'] = np.linalg.inv(np.array(source['T_cam_imu'])).ravel().tolist()
    config['live_camera_time_offset_ns'] = 0
    config['imu_params']['delay_imu_cam'] = 0.
    config['label'] = 'TideSense_Kalibr_residual_input_validation'
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('calibration', 'recording', 'base-calibration', 'binary', 'output'):
        parser.add_argument('--' + key, required=True, type=Path)
    parser.add_argument('--variants', nargs='+', default=['new-geometry', 'gyro-only', 'no-g-sensitivity', 'full'])
    parser.add_argument('--repeats', type=int, default=2)
    args = parser.parse_args()
    for key in ('calibration', 'recording', 'base_calibration', 'binary'):
        setattr(args, key, getattr(args, key).resolve(strict=True))
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    ma, mw, c, a = matrices(load(args.calibration / 'imucam-imu.yaml'))
    correction_checks(ma, mw, c, a)
    base = load(args.base_calibration)
    chain = load(args.calibration / 'imucam-camchain-imucam.yaml')
    config = camera_yaml(base, chain)
    (out / 'native.yaml').write_text(yaml.safe_dump(config, sort_keys=False))
    stereo = rows(args.recording / 'stereo.csv')
    metadata = load(args.recording / 'metadata.yaml')
    previous_offset = int(stereo[0][0]) - int(metadata['first_camera_ns'])
    offset = round(chain['cam0']['timeshift_cam_imu'] * 1e9)
    for row in stereo:
        row[0] = str(int(row[0]) + offset - previous_offset)
    imu = rows(args.recording / 'imu.csv')
    if not int(imu[0][0]) < int(stereo[0][0]) <= int(stereo[-1][0]) < int(imu[-1][0]):
        raise ValueError('Recalibrated timestamps are not covered by recorded IMU; no clipping or extrapolation allowed')
    # Input images and corrected IMU values are never modified in place.
    values = np.array([[float(v) for v in row[1:]] for row in imu])
    da, gw = np.linalg.inv(ma), c.T @ np.linalg.inv(mw)
    corrected_accel = values[:, 3:] @ da.T
    corrected_gyro = values[:, :3] @ gw.T
    summary = dict(calibration=str(args.calibration), recording=str(args.recording),
                   correction='a=inv(Ma)*am; w=C.T*inv(Mw)*(wm-A*C*a); no fitted bias subtraction',
                   source_input='Recorded IMU already has six-face correction; it is not applied again.',
                   noise='Original device continuous-time scalar noise coefficients held fixed.',
                   new_camera_offset_ns=offset, old_export_offset_ns=previous_offset,
                   imu_rows=len(imu), stereo_rows=len(stereo),
                   model_consistency_test_passed=True,
                   binary_sha256=sha256(args.binary), library_sha256=sha256(args.binary.parent / 'libschurvins.so'),
                   calibration_sha256={n: sha256(args.calibration / n) for n in ('imucam-imu.yaml', 'imucam-camchain-imucam.yaml')},
                   ma=ma.tolist(), mw=mw.tolist(), c_gyro_i=c.tolist(), a=a.tolist(), runs={})
    variants = {
        'new-geometry': values,
        'gyro-only': np.column_stack((corrected_gyro, values[:, 3:])),
        'no-g-sensitivity': np.column_stack((corrected_gyro, corrected_accel)),
        'full': np.column_stack(((values[:, :3] - corrected_accel @ c.T @ a.T) @ gw.T, corrected_accel)),
    }
    for name in args.variants:
        data = out / name; data.mkdir()
        for side in ('left', 'right'):
            (data / side).symlink_to(args.recording / side, target_is_directory=True)
        with (data / 'stereo.csv').open('w') as stream:
            stream.write('#timestamp_ns,left_image,right_image\n')
            for row in stereo:
                stream.write(','.join(row) + '\n')
        with (data / 'imu.csv').open('w') as stream:
            stream.write('#timestamp_ns,gx,gy,gz,ax,ay,az\n')
            for row, value in zip(imu, variants[name]):
                stream.write(row[0] + ',' + ','.join(format(v, '.17g') for v in value) + '\n')
        run = data / 'schur'; run.mkdir()
        command = [str(args.binary), str(out / 'native.yaml'), str(data / 'stereo.csv'), str(data / 'imu.csv'), str(run / 'poses.csv')]
        runtime = dict(SCHUR_MICRO_ROS='0', SCHUR_PREDICTION='0', SCHUR_TIMING_CSV=str(run / 'timing.csv'))
        env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}; env.update(runtime)
        save(run / 'command.json', dict(command=command, environment=runtime,
                                       imu_sha256=sha256(data / 'imu.csv'), stereo_sha256=sha256(data / 'stereo.csv')))
        results = []
        for repeat in range(args.repeats):
            with (run / 'run.log').open('w') as stream:
                subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=300)
            result = measure(run, stereo); results.append(result)
            repeat_dir = run / f'repeat-{repeat}'; repeat_dir.mkdir()
            for file in ('run.log', 'poses.csv', 'timing.csv'):
                shutil.copy2(run / file, repeat_dir / file)
            print(name, repeat, result['endpoint_displacement_m'], result['max_distance_from_first_pose_m'], flush=True)
        summary['runs'][name] = dict(results=results, repeat_pose_identity=len({r['poses_sha256'] for r in results}) == 1)
        save(out / 'summary.json', summary)


if __name__ == '__main__':
    main()

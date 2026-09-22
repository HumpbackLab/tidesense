#!/usr/bin/env python3
"""Convert Kalibr's calibrated stereo/IMU result to TideSense native SchurVINS YAML."""
import argparse
from pathlib import Path
import sys

import numpy as np
import yaml


def vector(value, size, name):
    result = np.asarray(value, dtype=float)
    if result.shape != (size,) or not np.isfinite(result).all():
        raise ValueError(f'{name}: expected {size} finite numbers')
    return result


def transform(value, name):
    result = np.asarray(value, dtype=float)
    if result.shape != (4, 4) or not np.isfinite(result).all():
        raise ValueError(f'{name}: expected finite 4x4 transform')
    rotation = result[:3, :3]
    if (not np.allclose(result[3], [0, 0, 0, 1], atol=1e-8, rtol=0)
            or not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-5, rtol=0)
            or abs(np.linalg.det(rotation) - 1) > 1e-5):
        raise ValueError(f'{name}: not a rigid transform')
    return result


def positive(value, name):
    result = float(value)
    if not np.isfinite(result) or result <= 0:
        raise ValueError(f'{name}: expected a positive finite value')
    return result


def matrix(value):
    value = np.asarray(value)
    return {'rows': value.shape[0], 'cols': value.shape[1], 'data': value.ravel().tolist()}


def convert(chain, imu_document):
    if not isinstance(chain, dict) or set(chain) != {'cam0', 'cam1'}:
        raise ValueError('Expected exactly cam0 (left) and cam1 (right)')
    if not isinstance(imu_document, dict):
        raise ValueError('Expected IMU YAML mapping')
    if 'imu0' in imu_document:
        if set(imu_document) != {'imu0'}:
            raise ValueError('Only a single IMU is supported')
        imu = imu_document['imu0']
    else:
        imu = imu_document
    if not isinstance(imu, dict):
        raise ValueError('Expected imu0 mapping')
    if imu.get('model', 'calibrated') != 'calibrated':
        raise ValueError('Only Kalibr calibrated IMU model is supported')
    if not np.allclose(transform(imu.get('T_i_b', np.eye(4)), 'T_i_b'),
                       np.eye(4), atol=1e-8, rtol=0):
        raise ValueError('T_i_b must be identity: body axes must be SC7U22 chip axes')
    if float(imu.get('time_offset', 0)) != 0:
        raise ValueError('Nonzero IMU time_offset is unsupported')
    rate = positive(imu['update_rate'], 'update_rate')
    if abs(rate - 200) > 10:
        raise ValueError('TideSense requires IMU sampling near 200 Hz')

    cameras, transforms, shifts = [], [], []
    for name in ('cam0', 'cam1'):
        cam = chain[name]
        if not isinstance(cam, dict):
            raise ValueError(f'{name}: expected camera mapping')
        if cam['camera_model'] != 'pinhole' or cam['distortion_model'] != 'radtan':
            raise ValueError(f'{name}: only pinhole-radtan is supported')
        if cam['resolution'] != [640, 400]:
            raise ValueError(f'{name}: live TideSense requires 640x400')
        intrinsics = vector(cam['intrinsics'], 4, f'{name}.intrinsics')
        if np.any(intrinsics[:2] <= 0):
            raise ValueError(f'{name}: focal lengths must be positive')
        distortion = vector(cam['distortion_coeffs'], 4, f'{name}.distortion_coeffs')
        t_cam_imu = transform(cam['T_cam_imu'], f'{name}.T_cam_imu')
        transforms.append(t_cam_imu)
        # Missing shift is intentional when Kalibr time calibration was disabled.
        shift = float(cam.get('timeshift_cam_imu', 0))
        if not np.isfinite(shift) or abs(shift) > 1:
            raise ValueError(f'{name}: camera time shift must be within +/-1 second')
        shifts.append(shift)
        cameras.append({
            'camera': {'label': name, 'id': f'tidesense-{name}',
                       'line-delay-nanoseconds': 0, 'image_width': 640,
                       'image_height': 400, 'type': 'pinhole',
                       'intrinsics': matrix(intrinsics[:, None]),
                       'distortion': {'type': 'radial-tangential',
                                      'parameters': matrix(distortion[:, None])}},
            'T_B_C': matrix(np.linalg.inv(t_cam_imu)),
        })
    relative = transforms[1] @ np.linalg.inv(transforms[0])
    if 'T_cn_cnm1' in chain['cam1'] and not np.allclose(
            relative, transform(chain['cam1']['T_cn_cnm1'], 'cam1.T_cn_cnm1'),
            atol=1e-5, rtol=0):
        raise ValueError('Stereo and camera-IMU transforms are inconsistent')
    if np.linalg.norm(relative[:3, 3]) <= 1e-6:
        raise ValueError('Stereo baseline is zero')
    parameters = {'delay_imu_cam': 0.0, 'max_imu_delta_t': 0.01,
                  'acc_max': 16 * 9.80665, 'omega_max': float(np.deg2rad(2000)),
                  'sigma_integration': 0.0, 'g': 9.80665, 'imu_rate': 200}
    for source, dest in (
            ('gyroscope_noise_density', 'sigma_omega_c'),
            ('accelerometer_noise_density', 'sigma_acc_c'),
            ('gyroscope_random_walk', 'sigma_omega_bias_c'),
            ('accelerometer_random_walk', 'sigma_acc_bias_c')):
        parameters[dest] = positive(imu[source], source)
    result = {'label': 'TideSense_Kalibr', 'id': 'tidesense-kalibr', 'cameras': cameras,
              'live_camera_time_offset_ns': round(shifts[0] * 1e9),
              'imu_params': parameters,
              'imu_initialization': {'velocity': [0.0]*3, 'omega_bias': [0.0]*3,
                                     'acc_bias': [0.0]*3, 'velocity_sigma': 2.0,
                                     'omega_bias_sigma': 0.01, 'acc_bias_sigma': 0.1}}
    return result, shifts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path, help='Completed Kalibr IMU-camera result directory')
    parser.add_argument('--output', type=Path, help='New YAML file (default RESULTS/schurvins.yaml)')
    args = parser.parse_args()
    try:
        camera_path = args.results / 'imucam-camchain-imucam.yaml'
        imu_path = args.results / 'imucam-imu.yaml'
        result, shifts = convert(yaml.safe_load(camera_path.read_text()),
                                 yaml.safe_load(imu_path.read_text()))
        text = ('# Source: imucam-camchain-imucam.yaml + imucam-imu.yaml\n'
                '# T_B_C = inverse(T_cam_imu), row-major; cam0=left, cam1=right.\n'
                '# Paired images use cam0 time shift; delay_imu_cam stays zero.\n'
                '# Six-face correction is loaded separately by LiveImu.\n'
                '# Initialization biases are residuals, not six-face offsets.\n'
                + yaml.safe_dump(result, sort_keys=False))
        output = args.output or args.results / 'schurvins.yaml'
        # Exclusive creation preserves previous configurations, including symlink targets.
        with output.open('x') as stream:
            stream.write(text)
    except (OSError, ValueError, TypeError, KeyError, yaml.YAMLError) as exc:
        parser.exit(2, f'SchurVINS export failed: {exc}\n')
    print(f'SchurVINS configuration: {output}')
    print(f'Camera shifts: left={shifts[0]*1e3:.6f} ms, right={shifts[1]*1e3:.6f} ms; '
          f'paired input uses left ({result["live_camera_time_offset_ns"]} ns).')
    if abs(shifts[0] - shifts[1]) > .001:
        print('WARNING: camera time shifts differ by >1 ms; check synchronization and calibration.',
              file=sys.stderr)
    print('Export validates format, not calibration accuracy; inspect the Kalibr report before use.')


if __name__ == '__main__':
    main()

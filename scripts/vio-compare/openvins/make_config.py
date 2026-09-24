#!/usr/bin/env python3
"""Adapt TideSense Kalibr files for OpenVINS and this recording."""

import argparse
from pathlib import Path
import yaml

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kalibr-dir', type=Path,
                    default=Path('/home/ncer/sophpi/cali/tidesense-imucam3-kalibr-20260924-101247'))
parser.add_argument('--output-dir', type=Path, default=HERE / 'config')
args = parser.parse_args()
KALIBR = args.kalibr_dir
OUT = args.output_dir
OUT.mkdir(parents=True, exist_ok=True)


def arr(values):
    return '[' + ', '.join(str(float(x)) for x in values) + ']'


source = (HERE / 'open_vins/config/euroc_mav/estimator_config.yaml').read_text()
for old, new in {
    'calib_cam_extrinsics: true': 'calib_cam_extrinsics: false',
    'calib_cam_intrinsics: true': 'calib_cam_intrinsics: false',
    'calib_cam_timeoffset: true': 'calib_cam_timeoffset: false',
    'num_opencv_threads: 4': 'num_opencv_threads: 1',
    'track_frequency: 21.0': 'track_frequency: 20.0',
    'histogram_method: "HISTOGRAM"': 'histogram_method: "NONE"',
}.items():
    source = source.replace(old, new)
(OUT / 'estimator_config.yaml').write_text(source)

cams = yaml.safe_load((KALIBR / 'imucam-camchain-imucam.yaml').read_text())
lines = ['%YAML:1.0']
for cam_id in (0, 1):
    c = cams[f'cam{cam_id}']
    matrix = [row[:] for row in c['T_cam_imu']]
    intrinsics = c['intrinsics'][:]
    distortion = c['distortion_coeffs'][:]
    if cam_id == 1:
        # Virtual right camera after 180 degree image rotation.
        matrix[0] = [-x for x in matrix[0]]
        matrix[1] = [-x for x in matrix[1]]
        intrinsics[2] = 639 - intrinsics[2]
        intrinsics[3] = 399 - intrinsics[3]
        distortion[2] = -distortion[2]
        distortion[3] = -distortion[3]
    lines += [f'cam{cam_id}:', '  T_cam_imu:']
    lines += [f'    - {arr(row)}' for row in matrix]
    lines += [
        f'  camera_model: {c["camera_model"]}',
        f'  distortion_model: {c["distortion_model"]}',
        f'  intrinsics: {arr(intrinsics)}',
        f'  distortion_coeffs: {arr(distortion)}',
        f'  resolution: [{int(c["resolution"][0])}, {int(c["resolution"][1])}]',
        '  timeshift_cam_imu: 0.0',
    ]
(OUT / 'kalibr_imucam_chain.yaml').write_text('\n'.join(lines) + '\n')

imu = yaml.safe_load((KALIBR / 'imucam-imu.yaml').read_text())['imu0']
lines = ['%YAML:1.0', 'imu0:', f'  model: {imu["model"]}']
for key in ('gyroscope_noise_density', 'gyroscope_random_walk',
            'accelerometer_noise_density', 'accelerometer_random_walk'):
    lines.append(f'  {key}: {imu[key]}')
for key in ('Tw', 'Ta', 'R_IMUtoACC', 'R_IMUtoGYRO', 'Tg'):
    lines.append(f'  {key}:')
    for index in range(3):
        row = [1.0 if index == column and key != 'Tg' else 0.0
               for column in range(3)]
        lines.append(f'    - {arr(row)}')
(OUT / 'kalibr_imu_chain.yaml').write_text('\n'.join(lines) + '\n')

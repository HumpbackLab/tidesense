#!/usr/bin/env python3
"""Replay fixed offline IMU calibration with isolated estimator ablations (no GT)."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from recording_ab import measure, rows, save, sha256

VARIANTS = {
    'baseline': {},
    'order': {'SCHUR_POINT_ID_ORDER': '1'},
    'id': {'SCHUR_FIX_OBSERVATION_ID': '1'},
    'reset': {'SCHUR_RESET_UNINITIALIZED': '1'},
    'id-reset': {'SCHUR_FIX_OBSERVATION_ID': '1', 'SCHUR_RESET_UNINITIALIZED': '1'},
    'id-reset-midpoint': {'SCHUR_FIX_OBSERVATION_ID': '1', 'SCHUR_RESET_UNINITIALIZED': '1',
                          'SCHUR_IMU_MIDPOINT': '1'},
    'id-reset-midpoint-order': {'SCHUR_FIX_OBSERVATION_ID': '1', 'SCHUR_RESET_UNINITIALIZED': '1',
                                'SCHUR_IMU_MIDPOINT': '1', 'SCHUR_POINT_ID_ORDER': '1'},
    'static': {'SCHUR_STATIC_INIT': '1'},
    'static-id': {'SCHUR_STATIC_INIT': '1', 'SCHUR_FIX_OBSERVATION_ID': '1'},
    'static-id-midpoint': {'SCHUR_STATIC_INIT': '1', 'SCHUR_FIX_OBSERVATION_ID': '1',
                          'SCHUR_IMU_MIDPOINT': '1'},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--validation-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--records', nargs='+', default=['084638', '003648'])
    parser.add_argument('--imu-variant', choices=['full', 'new-geometry', 'gyro-only'], default='full',
                        help='Use fixed IMU correction, recorded six-face-only IMU, or gyro-only correction')
    parser.add_argument('--variants', nargs='+', choices=VARIANTS,
                        default=['baseline', 'id-reset', 'id-reset-midpoint-order'])
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--debug', action='store_true')
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    args.binary = args.binary.resolve(strict=True)
    args.validation_root = args.validation_root.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    fingerprint = {'binary': sha256(args.binary),
                   'library': sha256(args.binary.parent / 'libschurvins.so')}
    report = {'binary': str(args.binary), 'sha256': fingerprint, 'debug': args.debug,
              'imu_variant': args.imu_variant,
              'metric': 'First-to-last distance, not ATE; neither recording has external GT.',
              'runs': {}}
    for record in args.records:
        source = args.validation_root / ('validation-equi-nogs-' + record)
        native = source / 'native.yaml'
        stereo, imu = source / args.imu_variant / 'stereo.csv', source / args.imu_variant / 'imu.csv'
        expected = rows(stereo)
        report['runs'][record] = {}
        for name in args.variants:
            results = []
            for repeat in range(args.repeats):
                out = args.output / record / name / str(repeat)
                out.mkdir(parents=True)
                runtime = {'SCHUR_MICRO_ROS': '0', 'SCHUR_PREDICTION': '0',
                           'SCHUR_TIMING_CSV': str(out / 'timing.csv'),
                           'LD_LIBRARY_PATH': str(args.binary.parent) + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
                           **VARIANTS[name]}
                if args.debug:
                    runtime.update(SCHUR_IMU_DEBUG_CSV=str(out / 'backend.csv'),
                                   SCHUR_CAUSE_CSV=str(out / 'eligibility.csv'),
                                   SCHUR_FRONT_DEBUG_CSV=str(out / 'frontend.csv'))
                command = [str(p) for p in (args.binary, native, stereo, imu, out / 'poses.csv')]
                save(out / 'command.json', {'command': command, 'environment': runtime,
                     'sha256': fingerprint, 'inputs': {str(p): sha256(p) for p in (native, stereo, imu)}})
                env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
                env.update(runtime)
                with (out / 'run.log').open('w') as stream:
                    subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                   check=True, timeout=300)
                result = measure(out, expected)
                save(out / 'summary.json', result)
                results.append(result)
                print(record, name, repeat, 'endpoint_m', result['endpoint_displacement_m'], flush=True)
            report['runs'][record][name] = {'results': results,
                'repeat_pose_identity': len({r['poses_sha256'] for r in results}) == 1}
            save(args.output / 'summary.json', report)


if __name__ == '__main__':
    main()

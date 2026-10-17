#!/usr/bin/env python3
"""Replay 084638 with fixed calibration; OpenVINS agreement is not ground truth.

Requires the adapter-only frame-marker probe from the timeline comparison.
Only midpoint IMU propagation and pixel residuals are retained.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess

import numpy as np
import compare_timeline as ct
import recording_ab as ra

ROOT = Path(__file__).resolve().parents[4]
DATA = ROOT / 'install/kalibr-intrinsics-20261017/validation-equi-nogs-084638'
CANDIDATE = dict(SCHUR_IMU_MIDPOINT='1', SCHUR_PIXEL_RESIDUAL='1')
VARIANTS = {'baseline': {},
            'midpoint': {'SCHUR_IMU_MIDPOINT': '1'},
            'pixel': {'SCHUR_PIXEL_RESIDUAL': '1'},
            'candidate': CANDIDATE}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'install/schur-openvins-timeline/probe/schurvins_stereo')
    parser.add_argument('--data', type=Path, default=DATA / 'full')
    parser.add_argument('--calibration', type=Path, default=DATA / 'native.yaml')
    parser.add_argument('--reference', type=Path, default=ROOT / 'install/schur-openvins-timeline/replay/084638/openvins/states.csv')
    parser.add_argument('--variants', nargs='+', choices=VARIANTS, default=['baseline', 'candidate'])
    args = parser.parse_args()
    ct.self_check()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary, library = args.binary.resolve(), args.library.resolve()
    expected = ra.rows(args.data / 'stereo.csv')
    origin = int(expected[0][0])
    reference = {int(r['timestamp_ns']): r for r in ct.read(args.reference) if r['raw_initialized'] == '1'}
    report = {}
    for name in args.variants:
        run = out / name
        run.mkdir()
        runtime = dict(SCHUR_MICRO_ROS='0', SCHUR_PREDICTION='0',
                       SCHUR_FRAME_MARKERS='1', SCHUR_TIMING_CSV=str(run / 'timing.csv'),
                       LD_LIBRARY_PATH=str(library) + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
                       OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
        runtime.update(VARIANTS[name])
        env = {k: v for k, v in os.environ.items() if not k.startswith('SCHUR_')}
        env.update(runtime)
        command = [str(x) for x in (binary, args.calibration.resolve(), args.data.resolve() / 'stereo.csv',
                                    args.data.resolve() / 'imu.csv', run / 'poses.csv')]
        ra.save(run / 'command.json', dict(command=command, environment=runtime,
            binary_sha256=ra.sha256(binary), library_sha256=ra.sha256(library / 'libschurvins.so'),
            calibration_sha256=ra.sha256(args.calibration),
            stereo_sha256=ra.sha256(args.data / 'stereo.csv'), imu_sha256=ra.sha256(args.data / 'imu.csv'),
            reference_sha256=ra.sha256(args.reference)))
        with (run / 'run.log').open('w') as log:
            subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
        result = ra.measure(run, expected)
        if not result['full_pose_coverage'] or not result['all_finite']:
            raise RuntimeError(f'{name}: incomplete or nonfinite trajectory')
        states = {int(r['timestamp_ns']): r for r in ct.read(run / 'poses.csv')}
        timestamps = np.array(sorted(states.keys() & reference.keys()), dtype=np.int64)
        t = (timestamps - origin) * 1e-9
        def field(rows, names):
            return np.array([[float(rows[int(stamp)][k]) for k in names] for stamp in timestamps])
        p = field(states, ('px', 'py', 'pz'))
        difference = ct.compare(t, p, field(states, ('qx', 'qy', 'qz', 'qw')),
            field(reference, ('px', 'py', 'pz')), field(reference, ('qx', 'qy', 'qz', 'qw')))
        result.update(disagreement_rmse_m=float(np.sqrt(np.mean(difference['position']**2))),
                      final_disagreement_m=float(difference['position'][-1]),
                      rpe_1s_mean_20_30_m=float(np.nanmean(difference['relative_position'][(t >= 20) & (t < 30)])))
        timing = ct.read(run / 'timing.csv')
        result['final_filter_speed_m_s'] = float(np.linalg.norm([float(timing[-1][k]) for k in ('vx', 'vy', 'vz')]))
        for start in (43, 44):
            tail = p[t >= start]
            result[f'terminal_travel_{start}_m'] = float(np.linalg.norm(tail[-1] - tail[0]))
            result[f'terminal_path_{start}_m'] = float(np.linalg.norm(np.diff(tail, axis=0), axis=1).sum())
        observations, stamp = {}, None
        for line in (run / 'run.log').read_text().splitlines():
            if line.startswith('FRAME_MARKER,'):
                stamp = int(line.split(',')[1])
            match = re.search(r'nObs = (\d+)', line)
            if match:
                observations[stamp] = int(match[1])
        result['surviving_observations_median_20_30'] = float(np.median(
            [v for stamp, v in observations.items() if 20 <= (stamp-origin)*1e-9 < 30]))
        with (run / 'comparison.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['time_s', 'position_difference_m', 'tilt_difference_deg', 'rpe_translation_1s_m'])
            writer.writerows(zip(t, difference['position'], difference['tilt'], difference['relative_position']))
        report[name] = result
        ra.save(run / 'summary.json', result)
        ra.save(out / 'summary.json', report)
        print(name, json.dumps(result), flush=True)


if __name__ == '__main__':
    main()

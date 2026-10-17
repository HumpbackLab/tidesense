#!/usr/bin/env python3
"""Compare two IMU pose estimates in time, without treating either as ground truth."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from scipy.spatial.transform import Rotation


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(line.lstrip('#') if i == 0 else line
                                   for i, line in enumerate(stream)))


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sustained_crossings(t, values, hold_s):
    """First uninterrupted interval at/above each threshold lasting hold_s."""
    result = {}
    for threshold in (0.1, 0.2, 0.5, 1.0, 2.0):
        start, event = None, None
        for i, value in enumerate(values):
            if not np.isfinite(value) or value < threshold:
                start = None
            elif start is None:
                start = i
            elif t[i] - t[start] >= hold_s:
                event = dict(first_s=float(t[start]), confirmed_s=float(t[i]),
                             frame_count=i - start + 1)
                break
        result[str(threshold)] = event
    return result


def compare(t, ps, qs, po, qo):
    rs = Rotation.from_quat(qs).as_matrix()
    # OpenVINS exports q_GtoI in JPL notation. JPL(q).T == Hamilton(q),
    # so the same xyzw numbers give world-from-IMU for both pose exports.
    ro = Rotation.from_quat(qo).as_matrix()
    initial = ro[0] @ rs[0].T
    yaw = np.arctan2(initial[1, 0] - initial[0, 1], initial[0, 0] + initial[1, 1])
    alignment = Rotation.from_rotvec([0, 0, yaw]).as_matrix()
    translation = po[0] - alignment @ ps[0]
    aligned = (alignment @ ps.T).T + translation
    orientation = Rotation.from_matrix(np.swapaxes(ro, 1, 2) @ (alignment @ rs)).magnitude()
    tilt = np.rad2deg(np.arccos(np.clip(np.sum(rs[:, 2, :] * ro[:, 2, :], axis=1), -1, 1)))
    position = np.linalg.norm(aligned - po, axis=1)
    full_position = np.linalg.norm((initial @ (ps - ps[0]).T).T - (po - po[0]), axis=1)
    velocity_s = np.gradient(aligned, t, axis=0)
    velocity_o = np.gradient(po, t, axis=0)
    relative_position, relative_angle = np.full(len(t), np.nan), np.full(len(t), np.nan)
    for i in range(len(t)):
        j = np.searchsorted(t, t[i] + 1.0)
        if j >= len(t) or t[j] - t[i] > 1.06:
            continue
        relative_position[i] = np.linalg.norm(rs[i].T @ (ps[j] - ps[i]) - ro[i].T @ (po[j] - po[i]))
        relative_angle[i] = Rotation.from_matrix((ro[i].T @ ro[j]).T @ (rs[i].T @ rs[j])).magnitude()
    return dict(position=position, orientation=np.rad2deg(orientation), tilt=tilt, full_position=full_position,
                relative_position=relative_position, relative_angle=np.rad2deg(relative_angle),
                velocity_difference=np.linalg.norm(velocity_s - velocity_o, axis=1),
                speed_s=np.linalg.norm(velocity_s, axis=1), speed_o=np.linalg.norm(velocity_o, axis=1),
                aligned_s=aligned, open_position=po, yaw_deg=float(np.rad2deg(yaw)),
                initial_tilt_deg=float(np.rad2deg(np.arccos(np.clip(initial[2, 2], -1, 1)))))


def probe_data(directory, original_poses):
    if not directory.exists():
        return {}, {}
    if (directory / 'poses.csv').read_bytes() != original_poses.read_bytes():
        raise ValueError('Diagnostic adapter changed the trajectory; its telemetry cannot be used here')
    states = {int(r['timestamp_ns']): r for r in read(directory / 'timing.csv')}
    inliers, timestamp = {}, None
    for line in (directory / 'run.log').read_text().splitlines():
        if line.startswith('FRAME_MARKER,'):
            timestamp = int(line.split(',')[1])
        match = re.search(r'nObs = (\d+)', line)
        if match:
            if timestamp is None or timestamp in inliers:
                raise ValueError('Missing/ambiguous frame marker for surviving feature count')
            inliers[timestamp] = int(match[1])
    return states, inliers


def self_check():
    t = np.linspace(0, 5, 101)
    p = np.column_stack((t, np.sin(t), 0.1 * t * t))
    q = Rotation.from_euler('xyz', np.column_stack((0.1 * t, 0.2 * t, 0.3 * t)))
    vector, scalar = q.as_quat()[37, :3], q.as_quat()[37, 3]
    x, y, z = vector
    skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    jpl = (2 * scalar * scalar - 1) * np.eye(3) - 2 * scalar * skew + 2 * np.outer(vector, vector)
    if np.linalg.norm(jpl.T - q.as_matrix()[37]) > 1e-12:
        raise ValueError('JPL/Hamilton pose convention mismatch')
    gauge = Rotation.from_euler('z', 71, degrees=True)
    transformed_p = gauge.apply(p) + np.array([4, -2, 1])
    d = compare(t, p, q.as_quat(), transformed_p, (gauge * q).as_quat())
    for key in ('position', 'orientation', 'relative_position', 'relative_angle'):
        if np.nanmax(d[key]) > 1e-10:
            raise ValueError('Known yaw/translation gauge was not removed: ' + key)
    # Short excursions must not become sustained onsets.
    pulse = np.zeros(len(t)); pulse[4:10] = 1; pulse[40:] = 0.6
    if sustained_crossings(t, pulse, 1.0)['0.5']['first_s'] != t[40]:
        raise ValueError('Sustained onset incorrectly accepted a short pulse')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replays', type=Path, required=True)
    parser.add_argument('--validation-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--records', nargs='+', default=['084638', '003648'])
    parser.add_argument('--hold-s', type=float, default=1.0)
    args = parser.parse_args()
    if args.hold_s <= 0:
        parser.error('--hold-s must be positive')
    self_check()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {'metric': 'Estimator disagreement, not ground-truth error or ATE.',
              'time': 'Seconds from first input stereo frame, including initialization.',
              'alignment': 'One initial yaw and translation; no scale/time/global trajectory fit.',
              'quaternions': 'Schur Hamilton IMU-to-world; OpenVINS JPL world-to-IMU transposed.',
              'hold_s': args.hold_s, 'runs': {}}
    for record in args.records:
        source = args.validation_root / ('validation-equi-nogs-' + record)
        data = args.replays / record
        native = data / 'schur/poses.csv'
        srows = read(native); sd = {int(x['timestamp_ns']): x for x in srows}
        stereo = read(source / 'full/stereo.csv')
        origin = int(stereo[0]['timestamp_ns'])
        frame_index = {int(x['timestamp_ns']): i for i, x in enumerate(stereo)}
        probe, inliers = probe_data(data / 'schur-probe', native)
        imu = np.loadtxt(source / 'full/imu.csv', delimiter=',')
        imu_t = (imu[:, 0] - origin) * 1e-9
        gyro = np.rad2deg(np.linalg.norm(imu[:, 1:4], axis=1))
        report['runs'][record] = {}
        for mode, folder in (('fixed', 'openvins'), ('online', 'openvins-online')):
            state_file = data / folder / 'states.csv'
            if not state_file.exists():
                continue
            orows = read(state_file)
            od = {int(x['timestamp_ns']): x for x in orows if x['raw_initialized'] == '1'}
            times = np.array(sorted(sd.keys() & od.keys()), dtype=np.int64)
            if len(times) < 3 or not set(times).issubset(frame_index):
                raise ValueError('Too few common poses or unexpected camera timestamps')
            t = (times - origin) * 1e-9
            def values(mapping, keys):
                return np.array([[float(mapping[int(stamp)][k]) for k in keys] for stamp in times])
            d = compare(t, values(sd, ('px', 'py', 'pz')), values(sd, ('qx', 'qy', 'qz', 'qw')),
                        values(od, ('px', 'py', 'pz')), values(od, ('qx', 'qy', 'qz', 'qw')))
            if not np.isfinite(d['position']).all():
                raise ValueError('Nonfinite trajectory')
            out = args.output / record / mode; out.mkdir(parents=True)
            with (out / 'timeline.csv').open('w') as stream:
                writer = csv.writer(stream)
                writer.writerow(['timestamp_ns', 'video_s', 'frame', 'position_difference_m',
                    'orientation_difference_deg', 'relative_translation_1s_m', 'relative_rotation_1s_deg',
                    'velocity_difference_m_s', 'schur_speed_fd_m_s', 'open_speed_fd_m_s',
                    'schur_current_kept_features', 'schur_quality', 'schur_x', 'schur_y', 'schur_z',
                    'open_x', 'open_y', 'open_z', 'tilt_difference_deg'])
                for i, stamp in enumerate(times):
                    key = int(stamp)
                    writer.writerow([key, t[i], frame_index[key], d['position'][i], d['orientation'][i],
                        d['relative_position'][i], d['relative_angle'][i], d['velocity_difference'][i],
                        d['speed_s'][i], d['speed_o'][i], inliers.get(key, ''),
                        probe[key]['quality'] if key in probe else '', *d['aligned_s'][i],
                        *d['open_position'][i], d['tilt'][i]])
            bins = []
            for lo in np.arange(np.floor(t[0] / 2) * 2, t[-1], 2):
                ix = (t >= lo) & (t < lo + 2)
                im = (imu_t >= lo) & (imu_t < lo + 2)
                if not ix.any():
                    continue
                surviving = [inliers[int(x)] for x in times[ix] if int(x) in inliers]
                bins.append(dict(start_s=float(lo), end_s=float(min(lo + 2, t[-1])),
                    position_median_m=float(np.median(d['position'][ix])),
                    position_max_m=float(d['position'][ix].max()),
                    orientation_median_deg=float(np.median(d['orientation'][ix])),
                    tilt_median_deg=float(np.median(d['tilt'][ix])),
                    gyro_max_deg_s=float(gyro[im].max()) if im.any() else None,
                    schur_current_kept_median=float(np.median(surviving)) if surviving else None,
                    schur_backend_calls_with_feature_log=len(surviving), comparable_frames=int(ix.sum())))
            summary = dict(common_rows=len(times), first_comparable_s=float(t[0]), last_video_s=float(t[-1]),
                initial_yaw_alignment_deg=d['yaw_deg'], initial_tilt_disagreement_deg=d['initial_tilt_deg'],
                final_position_difference_m=float(d['position'][-1]),
                maximum_position_difference_m=float(d['position'].max()),
                final_orientation_difference_deg=float(d['orientation'][-1]),
                sustained_position_thresholds_m=sustained_crossings(t, d['position'], args.hold_s),
                full_initial_rotation_sensitivity=sustained_crossings(t, d['full_position'], args.hold_s),
                bins=bins, input_sha256={str(p.resolve()): fingerprint(p)
                    for p in (native, state_file, source / 'native.yaml',
                              source / 'full/stereo.csv', source / 'full/imu.csv')})
            (out / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
            report['runs'][record][mode] = summary
            print(record, mode, json.dumps(summary['sustained_position_thresholds_m']), flush=True)
    (args.output / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()

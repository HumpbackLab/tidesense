#!/usr/bin/env python3
"""Overlapping Allan deviation of ROS 2 IMU rate/acceleration samples (SI units).

Uses acquisition stamps, never network receipt time. No interpolation, decimation,
linear detrending, unit conversion or bias/scale calibration is performed.
"""
import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np
import yaml

AXES = ['gyro_x', 'gyro_y', 'gyro_z', 'accel_x', 'accel_y', 'accel_z']


def read_imu(source, topic):
    from rosbags.highlevel import AnyReader
    from rosbags.typesys import Stores, get_typestore
    stamps, values = [], []
    with AnyReader([source], default_typestore=get_typestore(Stores.ROS2_HUMBLE)) as reader:
        connections = [c for c in reader.connections if c.topic == topic]
        if not connections or any(c.msgtype != 'sensor_msgs/msg/Imu' for c in connections):
            raise ValueError('Requested topic must exist and contain sensor_msgs/msg/Imu')
        for c, _, raw in reader.messages(connections=connections):
            msg = reader.deserialize(raw, c.msgtype)
            stamps.append(msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec)
            values.append([getattr(v, axis) for v in
                           (msg.angular_velocity, msg.linear_acceleration) for axis in 'xyz'])
    stamps = np.asarray(stamps, dtype=np.int64)
    values = np.asarray(values, dtype=float)
    if len(stamps) < 100 or stamps[0] < 0 or np.any(np.diff(stamps) <= 0):
        raise ValueError('Need >=100 IMU samples with strictly increasing, nonnegative header stamps')
    if not np.isfinite(values).all():
        raise ValueError('IMU contains non-finite angular velocity/acceleration')
    return stamps, values


def sampling(stamps):
    intervals = np.diff(stamps) * 1e-9
    dt = float(np.median(intervals))
    # Windows may not bridge missing samples or strongly irregular timing.
    breaks = np.flatnonzero(np.abs(intervals / dt - 1) > .2) + 1
    bounds = np.r_[0, breaks, len(stamps)]
    segments = [(int(a), int(b)) for a, b in zip(bounds[:-1], bounds[1:])]
    return dt, intervals, segments


def overlapping_adev(values, segments, factors):
    """Variance = mean((next_m_sample_mean - previous_m_sample_mean)^2) / 2.

    Pool squared differences only within continuous segments. Counts are overlapping
    differences, NOT independent samples/degrees of freedom or confidence intervals.
    """
    sums = [np.vstack((np.zeros((1, values.shape[1])),
                      np.cumsum(values[a:b] - np.mean(values[a:b], axis=0), axis=0)))
            for a, b in segments]
    deviations, counts, support = [], [], []
    for m in factors:
        total = np.zeros(values.shape[1])
        count, independent_pairs = 0, 0
        for s in sums:
            n = len(s) - 1
            if n < 2*m:
                continue
            delta = (s[2*m:] - 2*s[m:-m] + s[:-2*m]) / m
            total += np.sum(delta**2, axis=0)
            count += len(delta)
            independent_pairs += n // (2*m)
        deviations.append(np.sqrt(total / (2*count)) if count else np.full(values.shape[1], np.nan))
        counts.append(count)
        support.append(independent_pairs)
    return np.asarray(deviations), np.asarray(counts), np.asarray(support)


def fit_component(taus, adev, support, kind):
    """Conservative, explicit heuristic; unresolved components remain null.

    Require >=5 points spanning >=0.5 decade, >=10 nonoverlapping pair supports,
    free slope within 0.15 of expected and fixed-slope log10 RMS <=0.06 dex.
    White noise searches 0.02..1 s, bias walk >=1 s and beyond the curve minimum.
    """
    slope = -.5 if kind == 'white' else .5
    mask = np.isfinite(adev) & (adev > 0) & (support >= 10)
    mask &= (taus >= .02) & (taus <= 1.) if kind == 'white' else taus >= 1.
    if kind == 'walk':
        reliable = np.flatnonzero(np.isfinite(adev) & (adev > 0) & (support >= 10))
        if not len(reliable):
            return None
        mask &= taus > taus[reliable[np.argmin(adev[reliable])]]
    indices = np.flatnonzero(mask)
    candidates = []
    for left in range(len(indices)):
        for right in range(left + 5, len(indices) + 1):
            idx = indices[left:right]
            x, y = np.log10(taus[idx]), np.log10(adev[idx])
            span = x[-1] - x[0]
            if span < .5:
                continue
            measured_slope = float(np.polyfit(x, y, 1)[0])
            intercept = float(np.mean(y - slope*x))
            error = float(np.sqrt(np.mean((y - slope*x - intercept)**2)))
            if abs(measured_slope - slope) > .15 or error > .06:
                continue
            # Prefer widest supported interval, then smallest fixed-slope residual.
            coefficient = 10**intercept * (math.sqrt(3.) if kind == 'walk' else 1.)
            candidates.append((span, -error, dict(coefficient=coefficient,
                tau_start=float(taus[idx[0]]), tau_end=float(taus[idx[-1]]),
                free_slope=measured_slope, expected_slope=slope, log10_rms=error,
                points=len(idx), min_nonoverlapping_pairs=int(min(support[idx])))))
    return max(candidates, key=lambda c: c[:2])[2] if candidates else None


def analyze(stamps, values):
    dt, intervals, segments = sampling(stamps)
    longest = max(b-a for a, b in segments)
    max_factor = longest // 20  # Long tau tail requires at least ten disjoint pairs.
    if max_factor < 2:
        raise ValueError('No sufficiently long, regularly sampled segment for Allan analysis')
    factors = np.unique(np.geomspace(1, max_factor, 100).astype(int))
    taus = factors*dt
    adev, counts, support = overlapping_adev(values, segments, factors)
    fits = {name: {kind: fit_component(taus, adev[:, i], support, kind)
                   for kind in ('white', 'walk')} for i, name in enumerate(AXES)}
    warnings = []
    duration = float((stamps[-1]-stamps[0])*1e-9)
    if duration < 3600:
        warnings.append('Less than one hour: long-term bias random walk is preliminary; collect a much longer stationary recording.')
    if len(segments) > 1:
        warnings.append('Irregular intervals/gaps split the data into segments; Allan windows do not bridge these gaps.')
    # One-second block means flag obvious motion/changes without asserting that
    # low variation proves stationarity or constant temperature.
    block = max(1, round(1/dt))
    means = np.asarray([values[i:i+block].mean(0) for a, b in segments
                        for i in range(a, b-block+1, block)])
    if len(means) < 2:
        raise ValueError('Need at least two seconds of continuous data')
    excursion = np.ptp(means, axis=0)
    motion_suspected = bool(np.max(excursion[:3]) > .02 or np.max(excursion[3:]) > .2)
    if motion_suspected:
        warnings.append('One-second means vary by >0.02 rad/s or >0.2 m/s^2: possible motion/drift. Noise YAML withheld (null values).')
    mean_norm = float(np.mean(np.linalg.norm(values[:, 3:], axis=1)))
    if abs(mean_norm - 9.80665) > .3:
        warnings.append('Acceleration magnitude differs from 9.80665 by >0.3 m/s^2; Allan analysis does not calibrate bias/scale.')
    parameters = {}
    for prefix, axes in (('gyroscope', AXES[:3]), ('accelerometer', AXES[3:])):
        for suffix, kind in (('noise_density', 'white'), ('random_walk', 'walk')):
            resolved = [fits[axis][kind] for axis in axes]
            parameters[prefix+'_'+suffix] = (max(f['coefficient'] for f in resolved)
                if all(resolved) and not motion_suspected else None)
            if not all(resolved):
                warnings.append(f'{prefix}_{suffix}: no supported {kind} interval on all three axes; left null.')
    summary = dict(samples=len(stamps), duration_seconds=duration, sample_period_seconds=dt,
        sample_rate_hz=1/dt, mean_rate_hz=(len(stamps)-1)/duration,
        interval_seconds=dict(min=float(min(intervals)), median=dt,
                              p99=float(np.quantile(intervals, .99)), max=float(max(intervals))),
        segments=len(segments), longest_segment_samples=longest,
        tau_max_seconds=float(max(taus)), axis_order=AXES, mean=values.mean(0).tolist(),
        std=values.std(0).tolist(), one_second_mean_peak_to_peak=excursion.tolist(),
        acceleration_norm_mean=mean_norm, motion_suspected=motion_suspected,
        fits=fits, parameters=parameters, aggregation='maximum across XYZ; null unless all axes resolve',
        parameters_complete=all(v is not None for v in parameters.values()), warnings=warnings,
        method='overlapping Allan deviation of rate samples; white=N/sqrt(tau), bias walk=K*sqrt(tau/3)',
        fit_policy='>=5 points, >=0.5 decade, >=10 disjoint pairs; free slope tolerance 0.15, fixed-slope log10 RMS <=0.06; white 0.02..1s, walk >=1s after minimum; heuristic not confidence interval',
        temperature='Not present in sensor_msgs/Imu; temperature stability unverified')
    return summary, taus, adev, counts, support


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--imu-topic', default='/tidesense/imu')
    p.add_argument('--skip-seconds', type=float, default=0.)
    p.add_argument('--duration', type=float)
    args = p.parse_args()
    if not math.isfinite(args.skip_seconds) or args.skip_seconds < 0 or (args.duration is not None and
            (not math.isfinite(args.duration) or args.duration <= 0)):
        p.error('skip must be finite and nonnegative; duration must be finite and positive')
    stamps, values = read_imu(args.source, args.imu_topic)
    elapsed = (stamps-stamps[0])*1e-9
    keep = elapsed >= args.skip_seconds
    if args.duration is not None:
        keep &= elapsed < args.skip_seconds+args.duration
    stamps, values = stamps[keep], values[keep]
    if len(stamps) < 100:
        p.error('Selected interval contains fewer than 100 samples')
    summary, taus, adev, counts, support = analyze(stamps, values)
    summary.update(source=str(args.source), topic=args.imu_topic, skip_seconds=args.skip_seconds,
                   requested_duration=args.duration, first_header_ns=int(stamps[0]), last_header_ns=int(stamps[-1]))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'allan.json').write_text(json.dumps(summary, indent=2, allow_nan=False)+'\n')
    config = dict(rostopic=args.imu_topic, update_rate=summary['sample_rate_hz'], **summary['parameters'])
    (args.output/'imu.yaml').write_text('# Allan estimates for this recording; inspect allan-report.md and fit intervals.\n'
        '# null means unresolved/unsuitable, not zero. No automatic safety multiplier.\n'+yaml.safe_dump(config, sort_keys=False))
    with (args.output/'allan.csv').open('w') as f:
        writer = csv.writer(f)
        writer.writerow(['tau_seconds', *AXES, 'overlapping_differences', 'nonoverlapping_pair_support'])
        writer.writerows([[tau, *row, int(n), int(k)] for tau, row, n, k in zip(taus, adev, counts, support)])
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, panels = plt.subplots(2, 1, figsize=(10, 9), sharex=True)
    for group, panel in enumerate(panels):
        for i, color in zip(range(group*3, group*3+3), ('tab:red', 'tab:green', 'tab:blue')):
            panel.loglog(taus, adev[:, i], color=color, label=AXES[i])
            for kind in ('white', 'walk'):
                fit = summary['fits'][AXES[i]][kind]
                if fit:
                    t = np.array([fit['tau_start'], fit['tau_end']])
                    line = fit['coefficient'] / np.sqrt(t) if kind == 'white' else fit['coefficient']*np.sqrt(t/3)
                    panel.loglog(t, line, '--', color=color, linewidth=2)
        panel.set_ylabel('ADEV [rad/s]' if group == 0 else 'ADEV [m/s²]')
        panel.grid(True, which='both', alpha=.3)
        panel.legend()
    panels[-1].set_xlabel('Averaging time τ [s]')
    fig.suptitle('Overlapping Allan deviation — dashed: accepted noise-model fits')
    fig.tight_layout(rect=(0, 0, 1, .95))
    fig.savefig(args.output/'allan.png', dpi=160)
    fig.savefig(args.output/'allan.pdf')
    plt.close(fig)
    text = ['# IMU Allan analysis', '',
        f"Samples: {len(stamps)}; duration: {summary['duration_seconds']:.2f} s; rate: {summary['sample_rate_hz']:.3f} Hz.", '',
        'Heuristic fit estimates, not a calibration of IMU scale/bias. No temperature measurements are available.', '',
        '## Data checks', '',
        f"Acceleration norm mean: {summary['acceleration_norm_mean']:.6f} m/s².",
        f"One-second mean peak-to-peak [gyro XYZ, accel XYZ]: {summary['one_second_mean_peak_to_peak']}", '',
        '## Per-axis estimates', '', '| Axis | White density N | Bias random walk K |', '|---|---:|---:|']
    for name in AXES:
        vals = [summary['fits'][name][k] for k in ('white', 'walk')]
        text.append('| '+name+' | '+' | '.join(f"{v['coefficient']:.7g}" if v else 'unresolved' for v in vals)+' |')
    text += ['', 'White units: gyro rad/s/√Hz, accel m/s²/√Hz. Bias-walk units: gyro rad/s²/√Hz, accel m/s³/√Hz.', '',
        'Kalibr scalars use the maximum coefficient across three resolved axes. See allan.json for fitted ranges/slopes and diagnostics.',
        'Overlapping counts are correlated and are not statistical degrees of freedom. Fit acceptance is a heuristic, not proof of a physical noise model.', '',
        '## Warnings', '', *['- '+w for w in summary['warnings']], '',
        'Null YAML parameters cannot be used for camera–IMU calibration. Longer stable-temperature recordings may be needed; do not substitute the Allan minimum for bias random walk.', '',
        'Sources: [Kalibr noise model](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model), '
        '[overlapping Allan definition](https://allantools.readthedocs.io/en/stable/functions.html#allantools.oadev).']
    (args.output/'allan-report.md').write_text('\n'.join(text)+'\n')
    print(json.dumps(summary, indent=2, allow_nan=False))
    if not summary['parameters_complete']:
        print('Analysis complete, but imu.yaml has unresolved parameters; do not use it for Kalibr yet.')


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error))

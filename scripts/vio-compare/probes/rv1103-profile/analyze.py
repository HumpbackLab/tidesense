#!/usr/bin/env python3
"""Summarize initialized, post-warmup frames and optionally assign perf samples."""
import argparse
import bisect
import collections
import csv
import json
import math
import re
from pathlib import Path

def stats(values):
    values = sorted(values)
    def percentile(q):
        index = (len(values) - 1) * q
        lo = math.floor(index)
        hi = math.ceil(index)
        return values[lo] + (values[hi] - values[lo]) * (index - lo)
    return dict(n=len(values), mean=sum(values)/len(values),
                p50=percentile(.5), p95=percentile(.95), maximum=values[-1])

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('detail', type=Path)
parser.add_argument('--perf-script', type=Path)
parser.add_argument('--stages', type=Path)
parser.add_argument('--update-name', choices=['msckf', 'schur'], default='msckf')
args = parser.parse_args()
with args.detail.open() as file:
    all_frames = list(csv.DictReader(file))
frames = [row for row in all_frames if row['measured'] == '1']
if not frames:
    raise SystemExit('No measured frames')
summary = {'measured_frames': len(frames), 'timing_ms': {}}
for key in frames[0]:
    if key.endswith('_ms'):
        summary['timing_ms'][key] = stats([float(row[key]) for row in frames])
summary['timing_ms']['vio_plus_decode_ms'] = stats([
    float(row['vio_ms'])+float(row['decode_ms']) for row in frames])

if args.stages:
    with args.stages.open() as file:
        stages = [list(map(float, row)) for row in csv.reader(file)
                  if row and not row[0].startswith('#')]
    if len(stages) != sum(row['initialized'] == '1' for row in all_frames):
        raise ValueError('Stage/initialized-frame counts do not align')
    stages = stages[-len(frames):]
    columns = ['frontend_ms', 'propagation_ms', args.update_name+'_ms']
    if len(stages[0]) == 8:
        columns += ['slam_update_ms', 'slam_delayed_ms']
    elif len(stages[0]) != 6:
        raise ValueError('Unexpected stage column count')
    columns += ['marginalization_ms', 'internal_ms']
    # Stages use the estimated IMU clock; detail rows use camera timestamps.
    # Pair by execution order/count, and guard against a whole-frame mismatch.
    frame_step = min((int(b['timestamp_ns'])-int(a['timestamp_ns']))*1e-9
                     for a, b in zip(frames, frames[1:]))
    clock_offsets = []
    for detail, stage in zip(frames, stages):
        offset = stage[0]-int(detail['timestamp_ns'])*1e-9
        clock_offsets.append(offset*1000)
        if abs(offset) >= frame_step/2:
            raise ValueError('Detail/stage clocks differ by at least half a frame')
        if any(x < 0 for x in stage[1:]):
            raise ValueError('Negative wall-clock stage time')
    for index, key in enumerate(columns, 1):
        summary['timing_ms'][key] = stats([row[index]*1000 for row in stages])
    summary['timing_ms']['backend_ms'] = stats([
        sum(row[2:-1])*1000 for row in stages])
    summary['stage_imu_minus_camera_clock_ms'] = dict(minimum=min(clock_offsets),
                                                   maximum=max(clock_offsets))

if args.perf_script:
    starts = [int(row['start_ns']) for row in frames]
    ends = [int(row['end_ns']) for row in frames]
    counter = collections.Counter()
    samples = collections.Counter()
    total = included = 0
    # perf script --ns -F time,period,event,ip,sym,dso
    for line in args.perf_script.read_text().splitlines():
        fields = line.split()
        if len(fields) < 6 or not fields[0].endswith(':'):
            continue
        try:
            sec, ns = fields[0][:-1].split('.')
            timestamp = int(sec)*10**9 + int(ns.ljust(9, '0'))
            period = int(fields[1])
        except ValueError:
            continue
        total += 1
        index = bisect.bisect_right(starts, timestamp)-1
        if index < 0 or timestamp >= ends[index]:
            continue
        included += 1
        # Symbol names can include spaces; the last item is the DSO.
        symbol = re.sub(r'\+0x[0-9a-fA-F]+$', '', ' '.join(fields[4:-1]))
        counter[symbol] += period
        samples[symbol] += 1
    period_total = sum(counter.values())
    summary['perf'] = dict(total_samples=total, measured_vio_samples=included,
        cycles_period_total=period_total, symbols=[dict(symbol=symbol,
        percent=100*period/period_total, samples=samples[symbol])
        for symbol, period in counter.most_common(40)])
print(json.dumps(summary, indent=2))

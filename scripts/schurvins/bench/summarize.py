#!/usr/bin/env python3
"""Summarize completed on-board runs (first five VIO calls excluded)."""
import argparse
import csv
import hashlib
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('results', type=Path)
p.add_argument('--output', type=Path)
a = p.parse_args()
excluded_path = a.results/'excluded-runs.json'
excluded = json.loads(excluded_path.read_text()) if excluded_path.exists() else {}
runs = []
for path in sorted(a.results.glob('*.csv')):
    if path.stem in excluded or path.name.endswith('-poses.csv'):
        continue
    with path.open() as f:
        rows = list(csv.DictReader(f))
    if not rows or 'vio_ms' not in rows[0]:
        continue
    if len(rows) != 501:
        raise ValueError(f'{path}: expected 501 frames, got {len(rows)}')
    times = [float(r['vio_ms']) for r in rows[5:]]
    pose = path.with_name(path.stem + '-poses.csv')
    runs.append(dict(run=path.stem, variant=path.stem.split('-')[0], frames=len(rows),
                     measured_frames=len(times), tracking=sum(int(r['tracking']) for r in rows),
                     mean_ms=statistics.mean(times), median_ms=statistics.median(times),
                     p95_ms=sorted(times)[int(0.95*(len(times)-1))],
                     total_ms=sum(times),
                     pose_sha256=hashlib.sha256(pose.read_bytes()).hexdigest()))
baselines = [r['mean_ms'] for r in runs if r['variant'] == 'base']
if not baselines:
    raise ValueError('No completed, valid baseline run; run base before summarizing')
base = statistics.mean(baselines)
for r in runs:
    r['time_reduction_percent'] = 100*(1-r['mean_ms']/base)
    r['speedup'] = base/r['mean_ms']
kernels = []
for path in sorted(a.results.glob('*-20*-kernels.log')):
    lines = path.read_text().splitlines()
    if not lines or not lines[0].startswith('CHECK PASS'):
        raise ValueError(f'{path}: correctness checks did not pass')
    for r in csv.DictReader(lines[1:]):
        kernels.append(dict(run=path.stem.removesuffix('-kernels'),
                            variant=path.stem.split('-')[0],
                            image=Path(r['image']).name, kernel=r['kernel'],
                            ms_per_call=float(r['ms_per_call']), count=int(r['count'])))
result = dict(excluded_initial_frames=5, baseline_mean_ms=base, runs=runs,
              kernel_measurements=kernels, excluded_runs=excluded)
print(json.dumps(result, indent=2))
if a.output:
    a.output.write_text(json.dumps(result, indent=2)+'\n')

#!/usr/bin/env python3
"""Summarize profile-board.sh artifacts after host perf script decoding."""
import argparse
import bisect
import collections
import csv
import json
import re
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
p.add_argument('--warmup', type=int, default=10)
a = p.parse_args()
root = a.directory
rows = [{k: int(v) for k, v in r.items()} for r in csv.DictReader((root / 'stages.csv').open())][a.warmup:]
starts = [r['begin_ns'] for r in rows]
keys = ['begin_ns', 'rectify_end_ns', 'bm_end_ns', 'end_ns']
stages = ['rectify', 'bm', 'depth']
header = re.compile(r'^\s*(\d+)/(\d+)\s+(\d+)\.(\d+):\s+(\d+)\s+(\S+):\s*$')
frame = re.compile(r'^\s+([0-9a-f]+)\s+(.*?)\s+\((.*)\)\s*$')
counts = collections.Counter()
functions = collections.defaultdict(collections.Counter)
threads = collections.defaultdict(collections.Counter)
thread_functions = collections.defaultdict(collections.Counter)
phase = collections.defaultdict(collections.Counter)
phase_functions = collections.defaultdict(collections.Counter)
unknown = collections.Counter()
pending = None
first_sample = last_sample = None
main_pid = int((root / 'app.pid').read_text())
for line in (root / 'samples.txt').open():
    h = header.match(line)
    if h:
        pid, tid, sec, frac, period, event = h.groups()
        pending = (int(tid), int(sec)*10**9+int(frac.ljust(9, '0')), int(period), event.removesuffix(':u'))
        continue
    f = frame.match(line)
    if pending is None or f is None:
        continue
    tid, t, period, event = pending
    pending = None  # self cost only, ignore the rest of this callchain
    ip, symbol, dso = f.groups()
    first_sample = t if first_sample is None else min(first_sample, t)
    last_sample = t if last_sample is None else max(last_sample, t)
    counts[event] += 1
    functions[event][symbol] += period
    threads[event][str(tid)] += period
    thread_functions[(event, str(tid))][symbol] += period
    if symbol == '[unknown]':
        unknown[event] += period
    if tid != main_pid:
        continue
    index = bisect.bisect_right(starts, t)-1
    if index < 0:
        continue
    r = rows[index]
    if not r['begin_ns'] <= t < r['end_ns']:
        continue
    stage = stages[bisect.bisect_right([r[k] for k in keys], t)-1]
    phase[event][stage] += period
    phase_functions[(event, stage)][symbol] += period


def table(counter, limit=15):
    total = sum(counter.values())
    return [{'name': k, 'percent': 100*v/total, 'period': v} for k, v in counter.most_common(limit)] if total else []


def snapshot(path):
    lines = path.read_text().splitlines()
    uptime = float(lines[0].split()[0])
    cpu = next(line.split()[1:] for line in lines if line.startswith('cpu '))
    tasks = {}
    for line in lines:
        m = re.match(r'^(\d+) \((.*)\) (.*)$', line)
        if m:
            pid, name, rest = m.groups()
            fields = rest.split()
            tasks[pid] = {'name': name, 'user': int(fields[11]), 'system': int(fields[12])}
    return uptime, list(map(int, cpu)), tasks

before, after = snapshot(root / 'before.txt'), snapshot(root / 'after.txt')
seconds = after[0]-before[0]
cpu_delta = [y-x for x, y in zip(before[1], after[1])]
result = {
    'method': '499Hz/event PMU, weighted by periods, self costs; main-thread stages; warmup frames excluded; event modifiers and record command define user/system scope',
    'warmup_frames': a.warmup,
    'main_pid': main_pid,
    'sample_span_seconds': (last_sample-first_sample)/1e9,
    'cpu_snapshot_seconds': seconds,
    'system_busy_percent': 100*(1-(cpu_delta[3]+cpu_delta[4])/sum(cpu_delta)),
    'tasks': {}, 'timing': {}, 'events': {},
}
for pid, task in before[2].items():
    if pid in after[2]:
        end = after[2][pid]
        result['tasks'][pid] = {'name': task['name'],
            'user_percent': (end['user']-task['user'])/seconds,
            'kernel_percent': (end['system']-task['system'])/seconds}
# Linux-visible board CPU USER_HZ is 100. Tick deltas / seconds are percentages.
# Restrict wall/CPU statistics to frames fully inside the actual sampling window.
measured = [r for r in rows if r['begin_ns'] >= first_sample and r['end_ns'] <= last_sample]
result['measured_frames'] = len(measured)
for i, name in enumerate(stages):
    wall = [(r[keys[i+1]]-r[keys[i]])/1e6 for r in measured]
    cpu = [(r[keys[i+1].replace('_ns','_cpu_ns')]-r[keys[i].replace('_ns','_cpu_ns')])/1e6 for r in measured]
    result['timing'][name] = {'wall_ms': statistics.mean(wall), 'thread_cpu_ms': statistics.mean(cpu)}
wall = [(r['end_ns']-r['begin_ns'])/1e6 for r in measured]
cpu = [(r['end_cpu_ns']-r['begin_cpu_ns'])/1e6 for r in measured]
result['timing']['total'] = {'wall_ms': statistics.mean(wall), 'wall_p95_ms': sorted(wall)[int(.95*(len(wall)-1))],
                            'thread_cpu_ms': statistics.mean(cpu)}
result['processed_hz'] = ((len(measured)-1)*1e9/(measured[-1]['stamp_ns']-measured[0]['stamp_ns']))
for event in counts:
    result['events'][event] = {'samples': counts[event], 'functions': table(functions[event]),
        'threads': table(threads[event]), 'stages': table(phase[event]),
        'thread_functions': {t: table(thread_functions[(event, t)]) for t in threads[event]},
        'unknown_period_percent': 100*unknown[event]/sum(functions[event].values()),
        'stage_functions': {s: table(phase_functions[(event, s)]) for s in stages}}
(root / 'summary.json').write_text(json.dumps(result, indent=2)+'\n')
print(root.name, 'frames', len(measured), 'Hz', round(result['processed_hz'], 2),
      'system busy', round(result['system_busy_percent'], 2))
print('timing', result['timing'])
print('tasks', result['tasks'])
for event in counts:
    print(event, 'stages', result['events'][event]['stages'])
    print('top', result['events'][event]['functions'][:5])

#!/usr/bin/env python3
"""Assign user-only perf samples to frame stages, excluding 20 warmup frames."""
import argparse
import bisect
import collections
import csv
import json
import re
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("samples", type=Path)
p.add_argument("intervals", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
rows = list(csv.DictReader(a.intervals.open()))[20:]
stages = ["read", "rectify", "bm", "depth"]
keys = ["read_begin_ns", "rectify_begin_ns", "bm_begin_ns", "depth_begin_ns", "end_ns"]
windows = [tuple(int(r[k]) for k in keys) for r in rows]
starts = [v[0] for v in windows]
pattern = re.compile(r"^\s*(\d+)\.(\d+):\s+(\d+)\s+(\S+):\s+([0-9a-f]+)\s+(.*?)\s+\((.*)\)\s*$")
counts = collections.Counter()
weight = collections.Counter()
functions = collections.defaultdict(collections.Counter)
addresses = collections.defaultdict(collections.Counter)
all_functions = collections.defaultdict(collections.Counter)
for line in a.samples.open():
    m = pattern.match(line)
    if not m:
        continue
    sec, frac, period, event, ip, sym, dso = m.groups()
    t = int(sec) * 10**9 + int(frac.ljust(9, "0"))
    event = event.removesuffix(":u")
    counts[(event, "all")] += 1
    frame = bisect.bisect_right(starts, t) - 1
    if frame < 0 or t >= windows[frame][-1]:
        continue
    stage = stages[bisect.bisect_right(windows[frame], t) - 1]
    w = int(period)
    counts[(event, stage)] += 1
    weight[(event, stage)] += w
    functions[(event, stage)][sym] += w
    addresses[(event, stage)][ip] += w
    if stage != "read":
        all_functions[event][sym] += w

def table(counter):
    total = sum(counter.values())
    return [{"symbol": k, "period": v, "percent": 100*v/total} for k, v in counter.most_common()] if total else []

events = sorted({k[0] for k in counts})
out = {"method": "user-only PMU samples, 499 Hz/event, CLOCK_MONOTONIC stage windows, first 20 frames excluded; percentages use sample periods, self costs only", "measured_frames": len(rows), "events": {}}
for event in events:
    compute = sum(weight[(event, s)] for s in stages[1:])
    record = {"total_samples": counts[(event, "all")], "compute_period": compute,
              "compute_functions": table(all_functions[event]), "stages": {}}
    for s in stages:
        record["stages"][s] = {"samples": counts[(event, s)], "period": weight[(event, s)],
            "percent_of_compute": 100*weight[(event,s)]/compute if s != "read" and compute else None,
            "functions": table(functions[(event, s)]), "addresses": table(addresses[(event, s)])}
    out["events"][event] = record
    print(event, "compute stages:", [(s, round(record["stages"][s]["percent_of_compute"],2), record["stages"][s]["samples"]) for s in stages[1:]])
    print("BM self:")
    for row in record["stages"]["bm"]["functions"][:10]:
        print(f'{row["percent"]:6.2f}% {row["symbol"]}')
a.output.write_text(json.dumps(out, indent=2) + "\n")

#!/usr/bin/env python3
"""Compare complete frame periods and slow-frame components in a fixed RAM window."""
import argparse
import csv
import json
from pathlib import Path
import re

import numpy as np


def read(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def stats(values):
    values = np.asarray(values, dtype=float)
    return dict(mean=float(values.mean()), p95=float(np.percentile(values, 95)),
                max=float(values.max()), over_50ms=int((values > 50).sum()))


def summarize(folder, mode, first, last):
    rows = read(folder / (mode + ".csv"))
    log = (folder / (mode + ".log")).read_text(errors="replace")
    if f"DONE frames={len(rows)} " not in log:
        raise RuntimeError(f"{mode}: incomplete replay")
    if len(rows) <= last:
        raise RuntimeError(f"{mode}: window needs frame {last} as its end boundary")
    preload = re.search(r"PRELOAD first=(\d+) count=(\d+) pairs", log)
    if not preload or not int(preload[1]) <= first < last < int(preload[1]) + int(preload[2]):
        raise RuntimeError(f"{mode}: requested window and its end boundary must be preloaded")
    events = read(folder / (mode + "-trace.csv"))
    consumes = [e for e in events if e["kind"] == "consume_wait"]
    if len(consumes) != len(rows):
        raise RuntimeError(f"{mode}: expected one consume event per frame")
    for row, event in zip(rows, consumes):
        if abs(int(row["timestamp_ns"]) * 1e-9 - float(event["frame_stamp"])) > 1e-5:
            raise RuntimeError(f"{mode}: trace and pose timestamps disagree")
    starts = np.array([float(e["begin_ms"]) for e in consumes])
    periods = np.diff(starts)
    backends = [e for e in events if e["kind"] == "backend"]
    slow = []
    for i in sorted(range(first, last), key=lambda j: periods[j], reverse=True)[:6]:
        row, consume = rows[i], consumes[i]
        stamp = int(row["timestamp_ns"]) * 1e-9
        backend = min(backends, key=lambda e: abs(float(e["frame_stamp"]) - stamp))
        if abs(float(backend["frame_stamp"]) - stamp) > 1e-5:
            raise RuntimeError(f"{mode}: missing backend event for frame {i}")
        slow.append(dict(frame=i, timestamp_ns=row["timestamp_ns"], period_ms=float(periods[i]),
                         consume_ms=float(consume["end_ms"]) - float(consume["begin_ms"]),
                         geometry_ms=float(backend["begin_ms"]) - float(consume["end_ms"]),
                         backend_ms=float(row["tpu_backend_wall_ms"])))
    wall = re.search(r"DONE .*wall_s=([\d.]+)", log)
    window = stats(periods[first:last])
    window["fps"] = 1000 / window["mean"]
    return dict(frames=len(rows), initialized=sum(r["initialized"] == "1" for r in rows),
                whole_replay_wall_s=float(wall[1]) if wall else None, periods_ms=window,
                backend_ms=stats([float(r["tpu_backend_wall_ms"]) for r in rows[first:last]]),
                slow_frames=slow)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("results", type=Path)
    p.add_argument("--modes", nargs="+", required=True)
    p.add_argument("--first", type=int, default=1002)
    p.add_argument("--last", type=int, default=1038, help="Exclusive end; requires the next frame boundary")
    a = p.parse_args()
    if not 0 <= a.first < a.last:
        p.error("Expected 0 <= first < last")
    report = dict(window_zero_based=[a.first, a.last],
                  method="consume entrance to next consume entrance; includes inter-call work",
                  variants={m: summarize(a.results, m, a.first, a.last) for m in a.modes})
    (a.results / "tail-summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()

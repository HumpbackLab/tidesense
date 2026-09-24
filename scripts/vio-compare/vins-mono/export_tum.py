#!/usr/bin/env python3
"""Export only initialized VINS-Mono poses to TUM text (CLOCK_MONOTONIC seconds)."""
import csv
import sys

source, target = sys.argv[1:3]
with open(source, newline="") as inp, open(target, "w") as out:
    rows = csv.reader(line for line in inp if not line.startswith("#"))
    for row in rows:
        if len(row) < 10 or row[9] != "1":
            continue
        ts = int(round(float(row[0]))) / 1e9
        print(f"{ts:.9f} {' '.join(row[1:8])}", file=out)

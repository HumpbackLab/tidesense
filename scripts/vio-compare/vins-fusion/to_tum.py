#!/usr/bin/env python3
"""Convert VINS-Fusion offline body trajectory CSV to TUM format."""
import csv
import sys

with open(sys.argv[1], newline="") as source, open(sys.argv[2], "w") as destination:
    count = 0
    for row in csv.DictReader(source):
        destination.write(" ".join(row[key] for key in
            ("timestamp_s", "x", "y", "z", "qx", "qy", "qz", "qw")) + "\n")
        count += 1
    print(f"wrote {count} TUM poses")

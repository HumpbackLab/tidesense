#!/usr/bin/env python3
"""Select UZH-FPV stereo pairs nearest a 100 ms grid; retain all IMU rows."""
import bisect
import json
from pathlib import Path
import statistics


ROOT = Path(__file__).resolve().parents[4]
BASE = ROOT / "install/fisheye-benchmark/openvins"
SOURCE = BASE / "uzh-input-shm"
OUTPUT = BASE / "uzh-input-10hz"

lines = (SOURCE / "stereo.csv").read_text().splitlines()
rows = [line for line in lines if line and not line.startswith("#")]
timestamps = [int(line.split(",", 1)[0]) for line in rows]
indices = []
previous = -1
target = timestamps[0]
while target <= timestamps[-1]:
    j = bisect.bisect_left(timestamps, target)
    candidates = [k for k in (j - 1, j) if previous < k < len(rows)]
    if candidates:
        k = min(candidates, key=lambda candidate: abs(timestamps[candidate] - target))
        if not indices or timestamps[k] - timestamps[indices[-1]] >= 60_000_000:
            indices.append(k)
            previous = k
    target += 100_000_000

OUTPUT.mkdir(exist_ok=True)
for name in ("img", "imu.csv"):
    path = OUTPUT / name
    source = (SOURCE / name).resolve()
    if path.is_symlink():
        assert path.resolve() == source
    elif not path.exists():
        path.symlink_to(source)
    else:
        raise RuntimeError(f"Refusing to replace {path}")

(OUTPUT / "stereo.csv").write_text(lines[0] + "\n" + "\n".join(rows[k] for k in indices) + "\n")
gaps = [(timestamps[b] - timestamps[a]) * 1e-9 for a, b in zip(indices, indices[1:])]
manifest = {
    "source_frames": len(rows),
    "selected_frames": len(indices),
    "indices": indices,
    "average_fps": (len(indices) - 1) / sum(gaps),
    "median_gap_ms": statistics.median(gaps) * 1000,
    "max_gap_ms": max(gaps) * 1000,
}
(OUTPUT / "selection.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"{len(indices)} pairs; {manifest['average_fps']:.4f} Hz; all IMU retained")

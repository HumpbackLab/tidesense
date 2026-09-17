#!/usr/bin/env python3
"""Generate tiny synthetic CSV/PGM inputs for testing the native file reader."""
import pathlib
import random
import sys

out = pathlib.Path(sys.argv[1] if len(sys.argv) == 2 else "smoke-data")
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(42)
width, height = 320, 240
left = bytes(rng.randrange(256) for _ in range(width * height))
right = b"".join(left[y * width + 8:(y + 1) * width] + bytes(8) for y in range(height))
for name, pixels in (("left.pgm", left), ("right.pgm", right)):
    (out / name).write_bytes(b"P5\n320 240\n255\n" + pixels)
with (out / "stereo.csv").open("w") as f:
    f.write("#timestamp_ns,left,right\n")
    for i in range(6):
        f.write(f"{1_000_000_000 + i * 50_000_000},left.pgm,right.pgm\n")
with (out / "imu.csv").open("w") as f:
    f.write("#timestamp_ns,gx,gy,gz,ax,ay,az\n")
    for t in range(100_000_000, 1_300_000_001, 5_000_000):
        f.write(f"{t},0,0,0,0,0,9.81007\n")
print(out)

#!/usr/bin/env python3
"""Pack a stereo replay as lossless PNG for a board with limited SD space."""
import argparse
import csv
import hashlib
import json
import shutil
from pathlib import Path

import cv2
import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if source == output or source in output.parents or output in source.parents:
        raise SystemExit("source and output must be separate directories")
    with (source / "stereo.csv").open() as stream:
        reader = csv.reader(stream)
        header = next(reader)
        rows = list(reader)
    output.mkdir(parents=True, exist_ok=True)
    converted, manifest, names = [], [], set()
    for index, row in enumerate(rows):
        if len(row) != 3:
            raise RuntimeError(f"invalid stereo row {index + 2}")
        new_row = [row[0]]
        for name in row[1:]:
            relative = Path(name).with_suffix(".png")
            if relative.is_absolute() or ".." in relative.parts or relative in names:
                raise RuntimeError(f"invalid or duplicate output path: {relative}")
            names.add(relative)
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            image = cv2.imread(str(source / name), cv2.IMREAD_GRAYSCALE)
            if image is None:
                raise RuntimeError(f"cannot read {source / name}")
            if not cv2.imwrite(str(destination), image, [cv2.IMWRITE_PNG_COMPRESSION, 3]):
                raise RuntimeError(f"cannot write {destination}")
            decoded = cv2.imread(str(destination), cv2.IMREAD_GRAYSCALE)
            if not np.array_equal(image, decoded):
                raise RuntimeError(f"PNG changed pixels: {relative}")
            manifest.append(f"{hashlib.sha256(destination.read_bytes()).hexdigest()}  {relative.as_posix()}")
            new_row.append(relative.as_posix())
        converted.append(new_row)
        if (index + 1) % 250 == 0:
            print(f"verified {index + 1}/{len(rows)} stereo pairs", flush=True)
    with (output / "stereo.csv").open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(converted)
    shutil.copy2(source / "imu.csv", output / "imu.csv")
    for name in ("stereo.csv", "imu.csv"):
        manifest.append(f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}")
    (output / "SHA256SUMS").write_text("\n".join(manifest) + "\n")
    (output / "INPUT.json").write_text(json.dumps({
        "source": str(source), "stereo_pairs": len(rows),
        "images_verified_pixel_exact": 2 * len(rows),
        "format": "lossless PNG, grayscale pixels equal to original imread",
        "imu_sha256": hashlib.sha256((output / "imu.csv").read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    print(f"DONE: {len(rows)} pairs, {2 * len(rows)} pixel-exact PNG images", flush=True)


if __name__ == "__main__":
    main()

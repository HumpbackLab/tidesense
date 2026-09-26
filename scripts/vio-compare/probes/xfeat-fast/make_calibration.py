#!/usr/bin/env python3
"""Prepare representative normalized grayscale inputs for XFeat INT8 calibration."""

import argparse
import csv
from pathlib import Path

import cv2
import numpy as np


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--replay", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--count", type=int, default=32)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    with (args.replay / "stereo.csv").open(newline="") as file:
        rows = list(csv.DictReader((row for row in file if not row.startswith("#")),
                                   fieldnames=["t", "left", "right"]))
    paths = []
    indices = np.linspace(0, len(rows) - 1, args.count // 2, dtype=int)
    for index in indices:
        for camera in ("left", "right"):
            image = cv2.imread(str(args.replay / rows[index][camera]), cv2.IMREAD_GRAYSCALE)
            image = cv2.equalizeHist(image)
            h, w = image.shape
            image = cv2.resize(image, (w // 32 * 32, h // 32 * 32), interpolation=cv2.INTER_LINEAR)
            image = image.astype(np.float32)
            image = (image - image.mean()) / np.sqrt(image.var() + 1e-5)
            path = args.out / f"{camera}_{index:05d}.npz"
            np.savez(path, image_norm=image[None, None])
            paths.append(path)
    (args.out / "list.txt").write_text("".join(str(path.resolve()) + "\n" for path in paths))
    print(len(paths), "calibration images", paths[0])


if __name__ == "__main__":
    main()

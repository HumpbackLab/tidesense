#!/usr/bin/env python3
"""Precompute fixed-point rectification maps for the raw TideSense images."""
import argparse
import json
import struct
from pathlib import Path

import cv2
import numpy as np
import yaml

p = argparse.ArgumentParser()
p.add_argument("camchain", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
c = yaml.safe_load(a.camchain.read_text())
assert all(c[f"cam{i}"]["distortion_model"] == "equidistant" for i in range(2))
assert all(c[f"cam{i}"]["resolution"] == [640, 400] for i in range(2))
ks, ds = [], []
for i in range(2):
    fx, fy, cx, cy = c[f"cam{i}"]["intrinsics"]
    ks.append(np.array([[fx, 0, cx], [0, fy, cy], [0, 0, 1.]], dtype=np.float64))
    ds.append(np.array(c[f"cam{i}"]["distortion_coeffs"], dtype=np.float64))
# Rotate the right camera coordinate frame upright; later fold this rotation
# into the remap so the runtime consumes original, unrotated right PGM files.
s = np.diag([-1., -1., 1.])
t = np.array(c["cam1"]["T_cn_cnm1"], dtype=np.float64)
r, tr = s @ t[:3, :3], s @ t[:3, 3]
ks[1][0, 2] = 639 - ks[1][0, 2]
ks[1][1, 2] = 399 - ks[1][1, 2]
a.output.mkdir(parents=True, exist_ok=True)
metadata = {"camchain": str(a.camchain.resolve()), "opencv_map_generator": cv2.__version__,
            "baseline_m": float(np.linalg.norm(tr)), "right_rotation_folded_into_map": True,
            "balance": 0.0, "sizes": {}}
for w, h in [(320, 200), (640, 400)]:
    r1, r2, p1, p2, q = cv2.fisheye.stereoRectify(
        ks[0], ds[0], ks[1], ds[1], (640, 400), r, tr,
        flags=cv2.CALIB_ZERO_DISPARITY, newImageSize=(w, h), balance=0., fov_scale=1.)
    maps, valid = [], np.ones((h, w), dtype=np.uint8)
    for i, (ri, pi) in enumerate([(r1, p1), (r2, p2)]):
        mx, my = cv2.fisheye.initUndistortRectifyMap(ks[i], ds[i], ri, pi, (w, h), cv2.CV_32FC1)
        if i == 1:
            mx, my = 639 - mx, 399 - my
        valid &= ((mx >= 0) & (mx < 639) & (my >= 0) & (my < 399)).astype(np.uint8)
        maps.append(cv2.convertMaps(mx, my, cv2.CV_16SC2))
    baseline = abs(float(p2[0, 3] / p2[0, 0]))
    # With the transformed camera axes, disparity is negative for this rig.
    # Swapping the rectified images gives positive disparity. Depth then refers
    # to the rectified right camera; both cameras share the same rectified fx.
    swap = bool(p2[0, 3] > 0)
    if swap:
        maps.reverse()
    with (a.output / f"maps-{w}x{h}.bin").open("wb") as f:
        f.write(struct.pack("<IIdd", w, h, float(p1[0, 0]), baseline))
        for m1, m2 in maps:
            f.write(m1.astype("<i2").tobytes())
            f.write(m2.astype("<u2").tobytes())
        f.write(valid.tobytes())
    metadata["sizes"][f"{w}x{h}"] = {"fx": float(p1[0, 0]), "baseline_m": baseline,
        "swap_rectified_views": swap, "P1": p1.tolist(), "P2": p2.tolist(),
        "valid_map_fraction": float(valid.mean())}
    assert swap, "Runtime expects the positive-disparity view to be physical right"
(a.output / "maps.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(json.dumps(metadata, indent=2))

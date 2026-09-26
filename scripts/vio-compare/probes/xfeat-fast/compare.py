#!/usr/bin/env python3
"""Offline FAST+XFeat versus FAST+LK temporal association probe.

This is a pairwise front-end diagnostic, not an OpenVINS trajectory run.
Requires the upstream verlab/accelerated_features checkout and weights.
"""

import argparse
import csv
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F


def load_rows(path):
    with path.open(newline="") as file:
        return list(csv.DictReader((row for row in file if not row.startswith("#")), fieldnames=["t", "left", "right"]))


def select_pairs(rows, imu_rows, count):
    ts = np.array([int(r["t"]) for r in imu_rows], dtype=np.int64)
    gyro = np.array([[float(r[k]) for k in ("gx", "gy", "gz")] for r in imu_rows])
    speeds = np.linalg.norm(gyro, axis=1)
    pair_speeds = []
    for row in rows[1:]:
        t = int(row["t"])
        idx = int(np.clip(np.searchsorted(ts, t), 0, len(ts) - 1))
        pair_speeds.append(float(speeds[idx]))
    order = np.argsort(pair_speeds)
    # Spread the samples across each motion class, avoiding adjacent pairs.
    chosen = []
    for subset, label in ((order[: len(order) // 4], "low"), (order[3 * len(order) // 4 :], "high")):
        indices = np.linspace(0, len(subset) - 1, count, dtype=int)
        chosen.extend((int(subset[i]) + 1, label, pair_speeds[int(subset[i])]) for i in indices)
    return sorted(chosen)


def grid_fast(img, fast, target=200, grid=5):
    h, w = img.shape
    per_cell = (target + grid * grid - 1) // (grid * grid)
    points = []
    for gy in range(grid):
        for gx in range(grid):
            x0, x1 = gx * w // grid, (gx + 1) * w // grid
            y0, y1 = gy * h // grid, (gy + 1) * h // grid
            keypoints = sorted(fast.detect(img[y0:y1, x0:x1]), key=lambda k: k.response, reverse=True)
            for kp in keypoints[:per_cell]:
                x, y = kp.pt[0] + x0, kp.pt[1] + y0
                if 10 <= x < w - 10 and 10 <= y < h - 10:
                    points.append((x, y))
    return np.asarray(points, dtype=np.float32).reshape(-1, 2)


def descriptors(model, image, points):
    tensor = torch.from_numpy(image.copy()).float()[None, None]
    tensor, rh, rw = model.preprocess_tensor(tensor)
    t0 = time.perf_counter()
    fmap = F.normalize(model.net(tensor)[0], dim=1)
    infer_ms = (time.perf_counter() - t0) * 1000
    if len(points) == 0:
        return np.empty((0, 64), np.float32), infer_ms, 0.0
    scaled = points.copy()
    scaled[:, 0] /= rw
    scaled[:, 1] /= rh
    pos = torch.from_numpy(scaled)[None]
    t0 = time.perf_counter()
    desc = F.normalize(model.interpolator(fmap, pos, tensor.shape[2], tensor.shape[3]), dim=-1)
    sample_ms = (time.perf_counter() - t0) * 1000
    return desc[0].cpu().numpy(), infer_ms, sample_ms


def xfeat_matches(a, b, da, db, threshold):
    if len(a) == 0 or len(b) == 0:
        return np.empty((0, 2), np.float32), np.empty((0, 2), np.float32)
    similarity = da @ db.T
    forward = similarity.argmax(axis=1)
    backward = similarity.argmax(axis=0)
    ids = np.arange(len(a))
    keep = (backward[forward] == ids) & (similarity[ids, forward] >= threshold)
    return a[keep], b[forward[keep]]


def lk_matches(a_img, b_img, points):
    if len(points) == 0:
        return np.empty((0, 2), np.float32), np.empty((0, 2), np.float32)
    tracked, status, _ = cv2.calcOpticalFlowPyrLK(a_img, b_img, points, None, winSize=(15, 15), maxLevel=3,
                                                   criteria=(cv2.TERM_CRITERIA_COUNT | cv2.TERM_CRITERIA_EPS, 15, 0.01))
    h, w = b_img.shape
    good = (status.ravel() != 0) & (tracked[:, 0] >= 0) & (tracked[:, 0] < w) & (tracked[:, 1] >= 0) & (tracked[:, 1] < h)
    return points[good], tracked[good]


def geometry(a, b):
    if len(a) < 8:
        return 0
    _, mask = cv2.findFundamentalMat(a, b, cv2.FM_RANSAC, 2.0, 0.99)
    return int(mask.sum()) if mask is not None else 0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--replay", type=Path, required=True)
    p.add_argument("--xfeat", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--pairs-per-class", type=int, default=20)
    p.add_argument("--threshold", type=float, default=0.82)
    args = p.parse_args()
    sys.path.insert(0, str(args.xfeat))
    from modules.xfeat import XFeat

    cv2.setNumThreads(1)
    torch.set_num_threads(1)
    model = XFeat()
    fast = cv2.FastFeatureDetector_create(threshold=20, nonmaxSuppression=True)
    stereo = load_rows(args.replay / "stereo.csv")
    with (args.replay / "imu.csv").open(newline="") as file:
        imu = list(csv.DictReader((row for row in file if not row.startswith("#")), fieldnames=["t", "gx", "gy", "gz", "ax", "ay", "az"]))
    selected = select_pairs(stereo, imu, args.pairs_per_class)
    results = []
    for index, motion, gyro in selected:
        for camera in ("left", "right"):
            path0 = args.replay / stereo[index - 1][camera]
            path1 = args.replay / stereo[index][camera]
            img0 = cv2.imread(str(path0), cv2.IMREAD_GRAYSCALE)
            img1 = cv2.imread(str(path1), cv2.IMREAD_GRAYSCALE)
            if img0 is None or img1 is None:
                raise FileNotFoundError(path0 if img0 is None else path1)
            img0 = cv2.equalizeHist(img0)
            img1 = cv2.equalizeHist(img1)
            a = grid_fast(img0, fast)
            b = grid_fast(img1, fast)
            t0 = time.perf_counter()
            la, lb = lk_matches(img0, img1, a)
            lk_ms = (time.perf_counter() - t0) * 1000
            with torch.inference_mode():
                da, infer0, sample0 = descriptors(model, img0, a)
                db, infer1, sample1 = descriptors(model, img1, b)
            t0 = time.perf_counter()
            xa, xb = xfeat_matches(a, b, da, db, args.threshold)
            match_ms = (time.perf_counter() - t0) * 1000
            results.append(dict(index=index, camera=camera, motion=motion, gyro_rad_s=gyro,
                                fast_prev=len(a), fast_curr=len(b), lk_matches=len(la), lk_f_inliers=geometry(la, lb),
                                xfeat_matches=len(xa), xfeat_f_inliers=geometry(xa, xb), lk_ms=lk_ms,
                                xfeat_infer_ms=infer0 + infer1, xfeat_sample_ms=sample0 + sample1,
                                xfeat_match_ms=match_ms))
        print(f"{index}: {motion} {gyro:.2f} rad/s", flush=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(replay=str(args.replay), xfeat=str(args.xfeat), threshold=args.threshold,
                                       rows=results), indent=2) + "\n")
    for motion in ("low", "high"):
        subset = [r for r in results if r["motion"] == motion]
        print(motion, {k: round(float(np.mean([r[k] for r in subset])), 2) for k in
                       ("gyro_rad_s", "fast_prev", "lk_matches", "lk_f_inliers", "xfeat_matches", "xfeat_f_inliers",
                        "lk_ms", "xfeat_infer_ms", "xfeat_sample_ms", "xfeat_match_ms")})


if __name__ == "__main__":
    main()

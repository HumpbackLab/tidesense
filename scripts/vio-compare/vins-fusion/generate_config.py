#!/usr/bin/env python3
"""Convert latest Kalibr output to VINS-Fusion's OpenCV YAML files."""
import argparse
from pathlib import Path
import numpy as np
import yaml


def matrix_text(matrix):
    vals = ", ".join(f"{x:.16g}" for x in np.asarray(matrix).reshape(-1))
    return f"!!opencv-matrix\n   rows: 4\n   cols: 4\n   dt: d\n   data: [{vals}]"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("camchain", type=Path)
    parser.add_argument("imu", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--rotate-right", action="store_true")
    parser.add_argument("--board-output-path", default="/mnt/data/vio-compare/vins-fusion")
    args = parser.parse_args()
    cams = yaml.safe_load(args.camchain.read_text())
    imu = yaml.safe_load(args.imu.read_text())["imu0"]
    args.output.mkdir(parents=True, exist_ok=True)

    for name in ("cam0", "cam1"):
        cam = cams[name]
        k1, k2, p1, p2 = cam["distortion_coeffs"]
        fx, fy, cx, cy = cam["intrinsics"]
        width, height = cam["resolution"]
        if name == "cam1" and args.rotate_right:
            cx, cy = width - 1 - cx, height - 1 - cy
            p1, p2 = -p1, -p2
        text = f"""%YAML:1.0
---
model_type: PINHOLE
camera_name: {name}
image_width: {width}
image_height: {height}
distortion_parameters:
   k1: {k1:.16g}
   k2: {k2:.16g}
   p1: {p1:.16g}
   p2: {p2:.16g}
projection_parameters:
   fx: {fx:.16g}
   fy: {fy:.16g}
   cx: {cx:.16g}
   cy: {cy:.16g}
"""
        (args.output / f"{name}.yaml").write_text(text)

    t0 = np.linalg.inv(np.asarray(cams["cam0"]["T_cam_imu"], dtype=float))
    t1 = np.linalg.inv(np.asarray(cams["cam1"]["T_cam_imu"], dtype=float))
    if args.rotate_right:
        turn = np.diag([-1.0, -1.0, 1.0, 1.0])
        t1 = t1 @ turn
    text = f"""%YAML:1.0
---
imu: 1
num_of_cam: 2
imu_topic: /tidesense/imu
image0_topic: /tidesense/left/image_raw
image1_topic: /tidesense/right/image_raw
output_path: {args.board_output_path}
cam0_calib: cam0.yaml
cam1_calib: cam1.yaml
image_width: 640
image_height: 400
body_T_cam0: {matrix_text(t0)}
body_T_cam1: {matrix_text(t1)}
multiple_thread: 0
max_cnt: 150
min_dist: 25
F_threshold: 1.0
show_track: 0
flow_back: 1
max_solver_time: 0.04
max_num_iterations: 8
keyframe_parallax: 10.0
acc_n: {imu['accelerometer_noise_density']:.16g}
gyr_n: {imu['gyroscope_noise_density']:.16g}
acc_w: {imu['accelerometer_random_walk']:.16g}
gyr_w: {imu['gyroscope_random_walk']:.16g}
g_norm: 9.80665
estimate_extrinsic: 0
estimate_td: 0
td: 0.0
"""
    (args.output / "fusion.yaml").write_text(text)
    print(f"wrote {args.output / 'fusion.yaml'}")


if __name__ == "__main__":
    main()

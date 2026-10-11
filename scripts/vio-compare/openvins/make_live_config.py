#!/usr/bin/env python3
"""Convert TideSense SchurVINS calibration for raw live timestamps and OpenVINS."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np
import yaml

SDK = Path(__file__).resolve().parents[3]


def matrix(node):
    value = np.asarray(node["data"], dtype=float).reshape(node["rows"], node["cols"])
    if value.shape != (4, 4) or not np.isfinite(value).all():
        raise ValueError("T_B_C must be a finite 4x4 matrix")
    if not np.allclose(value[3], [0, 0, 0, 1], atol=1e-8):
        raise ValueError("Invalid homogeneous transform")
    rotation = value[:3, :3]
    if not np.allclose(rotation.T @ rotation, np.eye(3), atol=1e-5) or not np.isclose(np.linalg.det(rotation), 1, atol=1e-5):
        raise ValueError("Invalid camera rotation")
    return value


def opencv_yaml(path, value):
    # OpenCV FileStorage does not accept PyYAML's indentless nested sequences.
    lines = ["%YAML:1.0", "---"]
    for sensor, fields in value.items():
        lines.append(f"{sensor}:")
        for key, item in fields.items():
            lines.append(f"  {key}: " + json.dumps(item, allow_nan=False))
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schur", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--num-pts", type=int, default=150)
    parser.add_argument("--template", type=Path, default=Path(__file__).with_name("live-estimator-template.yaml"))
    args = parser.parse_args()
    if not 20 <= args.num_pts <= 2000:
        parser.error("num-pts must be between 20 and 2000")
    source = yaml.safe_load(args.schur.read_text())
    if len(source["cameras"]) != 2 or source["imu_params"]["imu_rate"] != 200:
        raise ValueError("Expected two cameras and 200 Hz IMU calibration")
    offset_ns = int(source["live_camera_time_offset_ns"])
    if abs(offset_ns) > 1_000_000_000:
        raise ValueError("Camera time offset exceeds one second")
    chain = {}
    body_cameras = []
    for index, item in enumerate(source["cameras"]):
        camera = item["camera"]
        width, height = int(camera["image_width"]), int(camera["image_height"])
        if (width, height) != (640, 400) or camera["type"] != "pinhole" or camera["distortion"]["type"] != "radial-tangential":
            raise ValueError("Live TideSense expects 640x400 pinhole/radtan cameras")
        intrinsics = np.asarray(camera["intrinsics"]["data"], dtype=float)
        distortion = np.asarray(camera["distortion"]["parameters"]["data"], dtype=float)
        if intrinsics.shape != (4,) or distortion.shape != (4,) or not np.isfinite(np.r_[intrinsics, distortion]).all() or np.any(intrinsics[:2] <= 0):
            raise ValueError("Invalid intrinsics or distortion")
        body_camera = matrix(item["T_B_C"])
        body_cameras.append(body_camera)
        camera_imu = np.linalg.inv(body_camera)
        if index == 1:
            camera_imu = np.diag([-1., -1., 1., 1.]) @ camera_imu
            intrinsics[2:] = [width - 1 - intrinsics[2], height - 1 - intrinsics[3]]
            distortion[2:] *= -1
        chain[f"cam{index}"] = dict(T_cam_imu=camera_imu.tolist(), camera_model="pinhole",
                                    distortion_model="radtan", intrinsics=intrinsics.tolist(),
                                    distortion_coeffs=distortion.tolist(), resolution=[width, height],
                                    timeshift_cam_imu=offset_ns * 1e-9)
    imu = source["imu_params"]
    imu_config = {"model": "calibrated"}
    for target, original in (("gyroscope_noise_density", "sigma_omega_c"),
                             ("gyroscope_random_walk", "sigma_omega_bias_c"),
                             ("accelerometer_noise_density", "sigma_acc_c"),
                             ("accelerometer_random_walk", "sigma_acc_bias_c")):
        value = float(imu[original])
        if not np.isfinite(value) or value <= 0:
            raise ValueError("IMU noise parameters must be finite and positive")
        imu_config[target] = value
    for name in ("Tw", "Ta", "R_IMUtoACC", "R_IMUtoGYRO", "Tg"):
        imu_config[name] = (np.zeros((3, 3)) if name == "Tg" else np.eye(3)).tolist()
    options = args.template.read_text()
    replacements = dict(num_pts=str(args.num_pts), num_opencv_threads="1", track_frequency="20.0",
                        calib_cam_intrinsics="false", calib_cam_extrinsics="false", calib_cam_timeoffset="false",
                        gravity_mag=str(float(imu["g"])), init_imu_thresh="0.3", init_max_disparity="2.0",
                        try_zupt="true", zupt_only_at_beginning="true", record_timing_information="false")
    initial = source.get("imu_initialization", {})
    for target, original in (("init_dyn_bias_g", "omega_bias"), ("init_dyn_bias_a", "acc_bias")):
        bias = np.asarray(initial.get(original, [0, 0, 0]), dtype=float)
        if bias.shape != (3,) or not np.isfinite(bias).all():
            raise ValueError("Invalid residual IMU bias")
        replacements[target] = json.dumps(bias.tolist())
    for name, value in replacements.items():
        options, count = re.subn(rf"^{re.escape(name)}:.*$", f"{name}: {value}", options, flags=re.M)
        if count != 1:
            raise ValueError(f"Template must contain {name} exactly once")
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    (out / "estimator_config.yaml").write_text(options)
    opencv_yaml(out / "kalibr_imucam_chain.yaml", chain)
    opencv_yaml(out / "kalibr_imu_chain.yaml", {"imu0": imu_config})
    manifest = dict(source=str(args.schur.resolve()), source_sha256=hashlib.sha256(args.schur.read_bytes()).hexdigest(),
                    num_pts=args.num_pts, precision="FP64 backend, original P H^T, FP64 covariance/solve/writeback",
                    right_image_rotation_degrees=180, camera_time_offset_ns=offset_ns,
                    timestamp_policy="raw left VI PTS; OpenVINS applies timeshift once; IMU samples bracket shifted time",
                    baseline_m=float(np.linalg.norm(body_cameras[1][:3, 3] - body_cameras[0][:3, 3])),
                    initialization="stationary startup with ZUPT only before first motion",
                    imu_noise_policy="copied directly, no additional multiplier", imu_noise=imu_config)
    (out / "calibration-source.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({key: value for key, value in manifest.items() if key != "imu_noise"}, indent=2))


if __name__ == "__main__":
    main()

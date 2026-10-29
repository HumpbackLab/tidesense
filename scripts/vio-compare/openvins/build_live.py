#!/usr/bin/env python3
"""Link the native live adapter against an already built A53 FP64 OpenVINS."""
import argparse
import hashlib
import json
import shlex
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
SDK = HERE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-build", type=Path, default=SDK / "install/openvins-backend-f32/a53-fp64/build")
    parser.add_argument("--output", type=Path, default=SDK / "install/openvins-live-fp64/bin/openvins_live")
    parser.add_argument("--micro-ros", choices=("on", "off"), default="on")
    args = parser.parse_args()
    core = args.core_build.resolve()
    cache = (core / "CMakeCache.txt").read_text()
    flags_path = core / "CMakeFiles/ov_msckf_lib.dir/flags.make"
    flags = flags_path.read_text()
    for option in ("OPENVINS_LK_WINDOW_16X15", "OPENVINS_LK_FILTER_VALID", "OPENVINS_SKIP_ACTIVE_VIZ"):
        if f"{option}:BOOL=ON" not in cache:
            raise RuntimeError(f"Required core option missing: {option}")
    for option in ("OPENVINS_FP32_BACKEND", "OPENVINS_EKF_MA_EXPERIMENT"):
        if f"-D{option}" in flags or f"{option}:BOOL=ON" in cache:
            raise RuntimeError(f"FP64 live adapter must not compile {option}")
    home = next(line.split("=", 1)[1] for line in cache.splitlines()
                if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="))
    source = Path(home).parent
    if "Eigen::MatrixXd _Cov;" not in (source / "ov_msckf/src/state/State.h").read_text():
        raise RuntimeError("Expected FP64 State covariance")
    consumer_flags = (core / "CMakeFiles/run_record.dir/flags.make").read_text()
    arguments = {}
    for line in consumer_flags.splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            arguments[key] = shlex.split(value)
    link = shlex.split((core / "CMakeFiles/run_record.dir/link.txt").read_text())
    compiler = link[0]
    if "aarch64" not in compiler:
        raise RuntimeError("Expected an A53 cross compiler")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    objects_dir = output.parent.parent / "build-live"
    objects_dir.mkdir(exist_ok=True)
    sensor = SDK / "scripts/schurvins"
    compile_flags = arguments["CXX_DEFINES"] + arguments["CXX_INCLUDES"] + arguments["CXX_FLAGS"]
    compile_flags += [f"-I{sensor}"]
    micro = SDK / "install/micro-ros/firmware/build"
    micro_flags = []
    if args.micro_ros == "on":
        if not (micro / "libmicroros.a").is_file():
            raise RuntimeError("Build micro-ROS first: scripts/schurvins/micro_ros/build.sh")
        micro_flags = ["-DSCHUR_WITH_MICRO_ROS=1", f"-I{micro / 'include'}"]
        micro_flags += [f"-I{p}" for p in sorted((micro / "include").iterdir()) if p.is_dir()]
    sources = [HERE / "run_live.cpp", sensor / "live_camera.cpp", sensor / "live_imu.cpp", sensor / "micro_ros_bridge.cpp"]
    objects = []
    for path in sources:
        obj = objects_dir / (path.stem + ".o")
        extra = micro_flags if path.name == "micro_ros_bridge.cpp" else []
        command = [compiler, *compile_flags, *extra, "-c", str(path), "-o", str(obj)]
        print("Compile", path, flush=True)
        subprocess.run(command, check=True)
        objects.append(str(obj))
    # Reuse the exact static dependency order recorded by the core CMake target.
    dependencies = []
    index = 1
    while index < len(link):
        token = link[index]
        if token == "-o":
            index += 2
            continue
        if not token.endswith(".o"):
            dependencies.append(token)
        index += 1
    yaml_library = SDK / "install/schurvins-gcc11-static/stage/lib/libyaml-cpp.a"
    if not yaml_library.is_file():
        raise RuntimeError("Missing A53 yaml-cpp dependency")
    command = [compiler, *objects, "-o", str(output), *dependencies, str(yaml_library)]
    if args.micro_ros == "on":
        command += [str(micro / "libmicroros.a")]
    command += ["-lpthread", "-ldl", "-lrt", "-lm"]
    print("Link", output, flush=True)
    subprocess.run(command, cwd=core, check=True)
    stripped = output.with_name(output.name + ".stripped")
    strip = compiler.removesuffix("g++") + "strip"
    subprocess.run([strip, "--strip-debug", "-o", str(stripped), str(output)], check=True)
    manifest = dict(core_build=str(core), precision="fp64", ekf_ma_mode="original", micro_ros=args.micro_ros,
                    c906_tpu_lk="OPENVINS_C906_TPU_LK:BOOL=ON" in cache,
                    c906_frontend="OPENVINS_C906_FRONTEND:BOOL=ON" in cache,
                    qr7point="OPENVINS_FRONT_QR7POINT:BOOL=ON" in cache,
                    c906_lk_dual_batch="OPENVINS_TPU_LK_DUAL_BATCH:BOOL=ON" in cache,
                    c906_lk_roi_cache="OPENVINS_TPU_LK_ROI_CACHE:BOOL=ON" in cache,
                    c906_latest_intrinsics="OPENVINS_FRONT_LATEST_INTRINSICS:BOOL=ON" in cache,
                    c906_lk_batch=next((line.split("=",1)[1] for line in cache.splitlines() if line.startswith("OPENVINS_TPU_LK_BATCH:STRING=")),"16"),
                    c906_frontend_early="OPENVINS_C906_FRONTEND_EARLY:BOOL=ON" in cache,
                    core_library_sha256=hashlib.sha256((core / "libov_msckf_lib.a").read_bytes()).hexdigest(),
                    binary_sha256=hashlib.sha256(stripped.read_bytes()).hexdigest(),
                    core_compile_flags=flags, link_command=command,
                    sources_sha256={str(p.relative_to(SDK)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (output.parent.parent / "BUILD-INFO.json").write_text(json.dumps(manifest, indent=2) + "\n")
    camera_helper = SDK / "install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static/bin/schurvins_camera"
    if not camera_helper.is_file():
        raise RuntimeError("Build the camera helper first: scripts/schurvins/build-camera.sh")
    for path in (camera_helper, SDK / "scripts/ov9281/fsin.sh", sensor / "prepare-imu.sh"):
        shutil.copy2(path, output.parent / path.name)
    shutil.copy2(HERE / "run-live.sh", output.parent.parent / "run-live.sh")
    (output.parent.parent / "run-live.sh").chmod(0o755)
    shutil.copy2(HERE / "README-live.md", output.parent.parent / "README.md")
    if "OPENVINS_C906_TPU_LK:BOOL=ON" in cache:
        shutil.copy2(HERE / "with-tpu.sh", output.parent.parent / "with-tpu.sh")
    if "OPENVINS_C906_FRONTEND:BOOL=ON" in cache:
        shutil.copy2(HERE / "run-front.sh", output.parent.parent / "run-front.sh")
        (output.parent.parent / "run-front.sh").chmod(0o755)
        shutil.copy2(HERE / "README-frontend.md", output.parent.parent / "README-frontend.md")
    if "OPENVINS_FRONT_QR7POINT:BOOL=ON" in cache:
        shutil.copy2(HERE.parent / "probes/qr7point/NOTICE", output.parent.parent / "NOTICE-OpenCV-QR7.txt")
    print(stripped)


if __name__ == "__main__":
    main()

#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../.." && pwd)
build="$sdk/install/tidesense-vio-depth-build"
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/tidesense-vio-depth"
stage="$sdk/install/schurvins-gcc11-static/stage"
[[ -f "$stage/lib/libopencv_calib3d.a" && -f "$sdk/install/micro-ros/firmware/build/libmicroros.a" ]] || {
  echo 'Build dependencies first: SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh' >&2;exit 1;
}
opts=()
for opt in HALFSAMPLE ZMSSD FAST10 ALIGN2D ALIGN2D_HESSIAN WARP_AFFINE; do
  key="SCHUR_OPT_${opt}_NEON";opts+=("-D$key=${!key:-ON}")
done
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$sdk/scripts/schurvins/a53-gcc11-toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$stage" -DSCHUR_STATIC=ON \
  -DSCHUR_OPT_WARP_TAIL2="${SCHUR_OPT_WARP_TAIL2:-ON}" "${opts[@]}"
cmake --build "$build" --target tidesense_vio_depth joint_depth_test -j"${JOBS:-4}"
"$sdk/scripts/schurvins/build-camera.sh"
mkdir -p "$out/bin" "$out/licenses"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
"${cross}strip" --strip-unneeded -o "$out/bin/tidesense_vio_depth" "$build/tidesense_vio_depth"
"${cross}strip" --strip-unneeded -o "$out/bin/joint_depth_test" "$build/joint_depth_test"
cp "$sdk/install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static/bin/"{schurvins_camera,fsin.sh} "$out/bin/"
cp "$sdk/scripts/schurvins/prepare-imu.sh" "$out/bin/"
cp "$sdk/scripts/schurvins/tidesense.yaml" "$out/calibration.yaml"
cp "$sdk/build/boards/cv181x/sg2002_tidesense_arm64_sd/sensor_cfg.ini" "$out/"
cp "$here/run.sh" "$here/README.md" "$here/VALIDATION.md" "$here/receive.py" "$out/"
case "${SCHUR_ROS_BACKEND:-micro-ros}" in
  native-dds) vio_package=schurvins-gcc11-native-dds ;;
  none) vio_package=schurvins-gcc11-static ;;
  *) vio_package=schurvins-gcc11-static-micro-ros ;;
esac
cp -a "$sdk/install/soc_sg2002_tidesense_arm64_sd/$vio_package/licenses/." "$out/licenses/"
mkdir -p "$out/licenses/micro-ROS"
cp -a "$sdk/install/micro-ros/firmware/build/licenses/." "$out/licenses/micro-ROS/"
{
  printf 'MODE=vio-depth\nSOURCE=%s\n' "$(git -C "$sdk/SchurVINS" rev-parse HEAD)"
  printf '%s\n' "CAMERA=OV9281_640x400_FSIN_20Hz" "VIO=SchurVINS_A53" "DEPTH=TPU_AREA_320x200_SAD32_15x15"
  printf '%s\n' "IMU=SC7U22_200Hz_raw_si" "MICRO_ROS=pose_depth_camera_info" "VIO_BUDGET=features60_seeds90_keyframes3_window3_detector_level1_FAST15" "CAMERA_CACHE_DEFAULT=1"
  for opt in HALFSAMPLE ZMSSD FAST10 ALIGN2D ALIGN2D_HESSIAN WARP_AFFINE; do
    key="SCHUR_OPT_${opt}_NEON";printf '%s=%s\n' "$key" "${!key:-ON}"
  done
  printf 'SCHUR_OPT_WARP_TAIL2=%s\n' "${SCHUR_OPT_WARP_TAIL2:-ON}"
} > "$out/BUILD-INFO"
(cd "$out" && sha256sum bin/* run.sh calibration.yaml sensor_cfg.ini > SHA256SUMS)
tar -C "$(dirname "$out")" -cf "$out-a53.tar" "$(basename "$out")"
echo "$out-a53.tar"

#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../.." && pwd)
stage="$sdk/install/schurvins-gcc11-static/stage"
build="$sdk/install/tidesense-depth-build"
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/tidesense-depth"
case "${DEPTH_STEREOBM_ALL64:-ON}" in
  ON|OFF) ;;
  *) echo 'DEPTH_STEREOBM_ALL64 must be ON or OFF' >&2; exit 2 ;;
esac
[[ -f "$stage/lib/libopencv_calib3d.a" && -f "$sdk/install/micro-ros/firmware/build/libmicroros.a" ]] || {
  echo 'Build dependencies first: SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh' >&2; exit 1;
}
"$sdk/tide_cam/build.sh"
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$here/../schurvins/a53-gcc11-toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$stage" -DDEPTH_BUILD_TESTS=ON \
  -DDEPTH_STEREOBM_ALL64="${DEPTH_STEREOBM_ALL64:-ON}"
cmake --build "$build" -j"${JOBS:-4}"
mkdir -p "$out/bin" "$out/licenses"
rm -f "$out/bin/schurvins_camera" "$out/bin/fsin.sh" "$out/sensor_cfg.ini"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
"${cross}strip" --strip-unneeded -o "$out/bin/tidesense_depth" "$build/tidesense_depth"
cp "$here/../schurvins/tidesense.yaml" "$out/calibration.yaml"
cp "$here/run.sh" "$here/README.md" "$here/VALIDATION.md" \
  "$here/PERFORMANCE.md" "$here/OPTIMIZATION.md" "$here/receive.py" "$out/"
cp "$sdk/install/schurvins-build/deps/opencv-3.4.20/LICENSE" "$out/licenses/OpenCV-LICENSE"
cp "$sdk/install/schurvins-build/deps/yaml-cpp/LICENSE" "$out/licenses/yaml-cpp-LICENSE"
cp -a "$sdk/install/micro-ros/firmware/build/licenses" "$out/licenses/micro-ros"
if [[ ${DEPTH_STEREOBM_ALL64:-ON} = ON ]]; then
  cp "$build/stereobm-all64.json" "$out/STEREOBM-BUILD.json"
else
  rm -f "$out/STEREOBM-BUILD.json"
fi
printf 'DEPTH_STEREOBM_ALL64=%s\nCAMERA_SOURCE=tide_cam\n' "${DEPTH_STEREOBM_ALL64:-ON}" > "$out/BUILD-INFO"
tar -C "$(dirname "$out")" -cf "$out-a53.tar" "$(basename "$out")"
echo "$out-a53.tar"

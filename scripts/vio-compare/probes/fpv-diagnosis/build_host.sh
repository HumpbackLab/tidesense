#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../../.." && pwd)
[[ $# == 2 ]] || { echo "Usage: $0 PATCHED_SOURCE BUILD_DIR" >&2; exit 2; }
source_dir=$(realpath "$1")
build_dir=$(realpath -m "$2")
# Reuse the existing native dependencies, not the A53 libraries.
host_root=${SCHUR_HOST_DEPS_ROOT:-"$sdk/install/dataset-analysis-20260924-155813"}
work="$host_root/pc-schurvins/work"
opencv="$host_root/pc/stage/share/OpenCV"
[[ -f "$work/stage/lib/libglog.so" && -d "$opencv" ]] || {
  echo 'Missing native dependencies; set SCHUR_HOST_DEPS_ROOT to the existing PC dependency workspace.' >&2
  exit 1
}
cmake -S "$sdk/scripts/schurvins" -B "$build_dir" \
  -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-I/usr/include/eigen3 \
  -DCMAKE_PROJECT_INCLUDE="$here/native.cmake" \
  -DSCHUR_SOURCE="$source_dir" -DSCHUR_STATIC=OFF \
  -DSCHUR_WITH_MICRO_ROS=OFF -DSCHUR_SYNC_OUTLIER_OBSERVATIONS=OFF \
  -DWORK="$work" -DGLOG_LIBRARY="$work/stage/lib/libglog.so" -DOpenCV_DIR="$opencv"
targets=(schurvins_stereo schurvins_midpoint_pixel_test schurvins_imu_intrinsics_test)
if grep -q 'RegisterCurrentFramePoints' "$source_dir/svo/include/svo/schur_vins.h"; then
  targets+=(schurvins_stereo_registration_test)
fi
cmake --build "$build_dir" --target "${targets[@]}" -j"${JOBS:-2}"

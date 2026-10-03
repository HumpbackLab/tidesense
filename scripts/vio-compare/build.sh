#!/usr/bin/env bash
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../.." && pwd)
work=${VIO_COMPARE_WORK:-"$sdk/install/vio-compare-20260924-121320"}
stage="$sdk/install/schurvins-gcc11-static/stage"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++"
jobs=${JOBS:-2}
target=${1:-all}
if [[ ${OPENVINS_FP32_BACKEND:-0} == 1 && ${OPENVINS_EKF_MA_EXPERIMENT:-0} == 1 ]]; then
  echo 'Choose OPENVINS_FP32_BACKEND or OPENVINS_EKF_MA_EXPERIMENT for this build.' >&2
  exit 2
fi

case "$target" in
  mono|fusion|openvins|ov-schurvins-host|all) ;;
  *) echo 'Usage: build.sh [mono|fusion|openvins|ov-schurvins-host|all]' >&2; exit 2 ;;
esac
if [[ "$target" == ov-schurvins-host ]]; then
  exec "$here/ov-schurvins/build-host.sh"
fi
for required in "$cross" "$stage/lib/libopencv_core.a" "$work/deps/ceres/lib/libceres.a"; do
  [[ -f "$required" ]] || { echo "Missing build dependency: $required" >&2; exit 1; }
done
"$here/sync.sh"

if [[ "$target" == mono || "$target" == all ]]; then
  cmake -S "$work/vins-mono/standalone" -B "$work/vins-mono/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cross" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
  cmake --build "$work/vins-mono/build" -j"$jobs"
fi
if [[ "$target" == fusion || "$target" == all ]]; then
  cmake -S "$work/vins-fusion" -B "$work/vins-fusion/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cross" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
  cmake --build "$work/vins-fusion/build" -j"$jobs"
fi
if [[ "$target" == openvins || "$target" == all ]]; then
  [[ -f "$work/openvins/deps/lib/libboost_system.a" ]] || {
    echo "Missing OpenVINS Boost 1.74 cross libraries in $work/openvins/deps" >&2; exit 1;
  }
  ov_source="$work/openvins/open_vins"
  ov_build="$work/openvins/build-a53"
  ov_precision_flags=()
  if [[ ${OPENVINS_FP32_BACKEND:-0} == 1 ]]; then
    python3 "$here/probes/backend-f32/prepare.py" \
      --source "$ov_source" --output "$work/openvins/fp32_source"
    ov_source="$work/openvins/fp32_source"
    ov_build="$work/openvins/build-a53-fp32"
    ov_precision_flags+=(-DOPENVINS_FP32_BACKEND=ON \
      "-DOPENVINS_FP32_EIGEN_HEADER=$here/probes/backend-f32/eigen_mixed.h")
  fi
  cmake -S "$ov_source/ov_msckf" -B "$ov_build" \
    -DCMAKE_TOOLCHAIN_FILE="$work/openvins/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS=OFF -DENABLE_ARUCO_TAGS=OFF \
    -DOPENVINS_SKIP_ACTIVE_VIZ="${OPENVINS_SKIP_ACTIVE_VIZ:-OFF}" \
    -DOPENVINS_EKF_MA_EXPERIMENT="${OPENVINS_EKF_MA_EXPERIMENT:-OFF}" \
    -DBOOST_ROOT="$work/openvins/deps" \
    -DOpenCV_DIR="$stage/share/OpenCV" \
    -DCeres_DIR="$work/deps/ceres/lib/cmake/Ceres" \
    -DEigen3_DIR=/usr/share/eigen3/cmake "${ov_precision_flags[@]}"
  cmake --build "$ov_build" --target run_record -j"$jobs"
fi

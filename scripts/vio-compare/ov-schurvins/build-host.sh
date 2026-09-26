#!/usr/bin/env bash
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work=${VIO_COMPARE_WORK:-"$sdk/install/vio-compare-20260924-121320"}
base="$work/ov-schurvins"

"$sdk/scripts/vio-compare/sync.sh"
cmake -S "$base/open_vins/ov_msckf" -B "$base/build-host" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-11 \
  -DENABLE_ROS=OFF -DENABLE_ARUCO_TAGS=OFF \
  -DOpenCV_DIR="$sdk/install/dataset-analysis-20260924-155813/pc/stage/share/OpenCV" \
  -DCeres_DIR=/home/ncer/fpga_vio/.tools/ceres-1.14/lib/cmake/Ceres \
  -DEigen3_DIR=/usr/share/eigen3/cmake
cmake --build "$base/build-host" --target run_record run_record_tidesense -j"${JOBS:-2}"

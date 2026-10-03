#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../../.." && pwd)
arch=${1:-a53}
precision=${2:-fp32}
case "$arch" in a53|host) ;; *) exit 2 ;; esac
case "$precision" in fp64|mixed|fp32) ;; *) exit 2 ;; esac
work=${OPENVINS_FP32_WORK:-"$sdk/install/openvins-backend-f32"}
variant="$work/$arch-$precision"
upstream="$sdk/install/vio-compare-20260924-121320/openvins/open_vins"
mkdir -p "$variant/base_source"
if [[ ! -f "$variant/base_source/ov_msckf/src/state/State.cpp" ]]; then
  git -C "$upstream" archive HEAD | tar xf - -C "$variant/base_source"
  patch -d "$variant/base_source" -p1 < "$sdk/scripts/vio-compare/patches/openvins-offline.patch"
  patch -d "$variant/base_source" -p1 < "$sdk/scripts/vio-compare/patches/openvins-headless.patch"
  if [[ "$precision" == mixed ]]; then
    patch -d "$variant/base_source" -p1 < "$sdk/scripts/vio-compare/probes/ekf-f32/openvins-core.patch"
    patch -d "$variant/base_source" -p1 < "$sdk/scripts/vio-compare/probes/ekf-f32/openvins-cmake.patch"
  fi
  cat >> "$variant/base_source/ov_msckf/CMakeLists.txt" <<'EOF'
add_executable(run_fisheye ${CMAKE_CURRENT_SOURCE_DIR}/../../run_fisheye.cpp)
target_link_libraries(run_fisheye ov_msckf_lib ${thirdparty_libraries})
if (CMAKE_CROSSCOMPILING)
  set_target_properties(run_fisheye PROPERTIES LINK_FLAGS "-static")
else ()
  set_target_properties(run_record PROPERTIES LINK_FLAGS "")
endif ()
EOF
fi
if ! cmp -s "$here/numeric_probe.h" "$variant/numeric_probe.h"; then
  cp "$here/numeric_probe.h" "$variant/numeric_probe.h"
fi
python3 - "$variant" "$sdk" <<'PY'
from pathlib import Path
import sys
sources={'run_record.cpp':'scripts/vio-compare/openvins/run_record.cpp',
         'run_fisheye.cpp':'install/fisheye-benchmark/openvins/run_fisheye.cpp'}
for name, relative in sources.items():
    p=Path(sys.argv[1])/name
    text=(Path(sys.argv[2])/relative).read_text().replace('#include "state/State.h"', '#include "state/State.h"\n#include "numeric_probe.h"')
    lines=text.splitlines()
    for i,line in enumerate(lines):
        if 'timestamp_ns,initialized,' in line:
            lines[i]=line.replace('\\n"', ',cov_dim,cov_min_diag,cov_asym_max,cov_min_eigen\\n"')
        if "poses << ',' << elapsed_ms" in line:
            lines[i]=line.replace(" << '\\n';", ";")+'\n      append_numeric_probe(poses, system.get_state(), frame_count);\n      poses << \'\\n\';'
    text='\n'.join(lines)+'\n'
    if not p.exists() or p.read_text()!=text:
        p.write_text(text)
PY
source="$variant/base_source"
flags=(-DCMAKE_BUILD_TYPE=Release -DENABLE_ROS=OFF -DENABLE_ARUCO_TAGS=OFF -DOPENVINS_SKIP_ACTIVE_VIZ=ON
  -DEigen3_DIR=/usr/share/eigen3/cmake)
if [[ "$precision" == fp32 ]]; then
  python3 "$here/prepare.py" --source "$source" --output "$variant/fp32_source"
  source="$variant/fp32_source"
  flags+=(-DOPENVINS_FP32_BACKEND=ON "-DOPENVINS_FP32_EIGEN_HEADER=$here/eigen_mixed.h")
elif [[ "$precision" == mixed ]]; then
  flags+=(-DOPENVINS_EKF_MA_EXPERIMENT=ON)
fi
if [[ "$arch" == a53 ]]; then
  flags+=("-DCMAKE_TOOLCHAIN_FILE=$sdk/scripts/vio-compare/openvins/toolchain.cmake"
    "-DBOOST_ROOT=$sdk/install/vio-compare-20260924-121320/openvins/deps"
    "-DOpenCV_DIR=$sdk/install/schurvins-gcc11-static/stage/share/OpenCV"
    "-DCeres_DIR=$sdk/install/vio-compare-20260924-121320/deps/ceres/lib/cmake/Ceres")
else
  flags+=(-DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11
    "-DOpenCV_DIR=$sdk/install/dataset-analysis-20260924-155813/pc/stage/share/OpenCV"
    -DCeres_DIR=/home/ncer/fpga_vio/.tools/ceres-1.14/lib/cmake/Ceres)
fi
cmake -S "$source/ov_msckf" -B "$variant/build" "${flags[@]}"
cmake --build "$variant/build" --target run_record run_fisheye -j"${JOBS:-4}"
echo "$variant/build/run_record"

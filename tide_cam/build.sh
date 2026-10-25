#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd "$here/.." && pwd)
# SDK archive generation uses the repository's supported board configuration.
(
 set +u # SDK envsetup references optional, unset environment variables.
 cd "$sdk"
 source build/envsetup_soc.sh >/dev/null
 defconfig sg2002_tidesense_arm64_sd >/dev/null
 make -C cvi_mpi component
 make -C cvi_mpi/sample/common
)
build=${TIDE_CAM_BUILD_DIR:-"$sdk/install/tide_cam-build"}
prefix=${TIDE_CAM_INSTALL_DIR:-"$sdk/install/tide_cam"}
toolchain=${TIDE_CAM_TOOLCHAIN:-"$here/cmake/sg2002-gcc6.cmake"}
cmake -S "$here" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
 -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$build" -j"${JOBS:-4}"
cmake --install "$build"

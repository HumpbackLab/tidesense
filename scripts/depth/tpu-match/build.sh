#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
cmake -S "$here" -B "$sdk/install/depth-tpu-match/build" \
  -DCMAKE_TOOLCHAIN_FILE="$sdk/scripts/schurvins/a53-gcc11-toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$sdk/install/schurvins-gcc11-static/stage" \
  -DCMAKE_BUILD_TYPE=Release -DDEPTH_STEREOBM_ALL64=ON
cmake --build "$sdk/install/depth-tpu-match/build" -j"${JOBS:-4}"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
"${cross}strip" --strip-unneeded -o "$sdk/install/depth-tpu-match/tpu_sad_probe" \
  "$sdk/install/depth-tpu-match/build/tpu_sad_probe"

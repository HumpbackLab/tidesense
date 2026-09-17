#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
work="$sdk/install/schurvins-gcc11-static"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
if [[ $# == 0 ]]; then set -- base all; fi
for variant in "$@"; do
  [[ $variant == base || $variant == all || $variant == align ]] || { echo 'Usage: build.sh [base|all|align ...]' >&2; exit 2; }
  enable=OFF; align_enable=OFF; tree="$work/opt-scalar"
  if [[ $variant != base ]]; then enable=ON; tree="$work/opt-all"; fi
  if [[ $variant == align ]]; then align_enable=ON; fi
  cmake -S "$sdk/scripts/schurvins" -B "$tree" \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/scripts/schurvins/a53-gcc11-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$work/stage" -DSCHUR_STATIC=ON \
    -DSCHUR_OPT_HALFSAMPLE_NEON="$enable" -DSCHUR_OPT_ZMSSD_NEON="$enable" \
    -DSCHUR_OPT_FAST10_NEON="$enable" -DSCHUR_OPT_ALIGN2D_NEON="$align_enable" \
    -DSCHUR_OPT_ALIGN2D_HESSIAN_NEON=OFF -DSCHUR_OPT_WARP_AFFINE_NEON=OFF
  cmake --build "$tree" -j"${JOBS:-4}" --target schurvins_stereo
  mkdir -p "$work/profile/$variant"
  cp "$tree/schurvins_stereo" "$work/profile/$variant/symbols"
  "${cross}strip" -o "$work/profile/$variant/stereo" "$work/profile/$variant/symbols"
done

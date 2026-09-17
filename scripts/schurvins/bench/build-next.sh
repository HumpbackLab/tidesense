#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$(cd -- "$here/../.." && pwd)
work="$sdk/install/schurvins-gcc11-static"
tree="$work/opt-all"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
if [[ $# == 0 ]]; then set -- base hessian warp both; fi
for variant in "$@"; do
 h=OFF; w=OFF
 case "$variant" in base);;hessian)h=ON;;warp)w=ON;;both)h=ON;w=ON;;*)exit 2;;esac
 cmake -S "$here" -B "$tree" \
  -DCMAKE_TOOLCHAIN_FILE="$here/a53-gcc11-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$work/stage" \
  -DSCHUR_STATIC=ON -DSCHUR_BUILD_BENCHMARKS=ON \
  -DSCHUR_OPT_HALFSAMPLE_NEON=ON -DSCHUR_OPT_ZMSSD_NEON=ON \
  -DSCHUR_OPT_FAST10_NEON=ON -DSCHUR_OPT_ALIGN2D_NEON=ON \
  -DSCHUR_OPT_ALIGN2D_HESSIAN_NEON="$h" -DSCHUR_OPT_WARP_AFFINE_NEON="$w"
 cmake --build "$tree" -j"${JOBS:-4}"
 out="$work/next-binaries/$variant";mkdir -p "$out"
 for program in stereo kernel_bench align2d_bench warp_bench; do
  "${cross}strip" -o "$out/schurvins_$program" "$tree/schurvins_$program"
 done
 cp "$tree/schurvins_stereo" "$out/symbols"
 printf 'HALFSAMPLE=ON\nZMSSD=ON\nFAST10=ON\nALIGN2D=ON\nALIGN2D_HESSIAN=%s\nWARP_AFFINE=%s\nSOURCE=%s\n' "$h" "$w" "$(git -C "$sdk/SchurVINS" rev-parse HEAD)" > "$out/BUILD-INFO"
 (cd "$out" && sha256sum schurvins_kernel_bench schurvins_stereo > SHA256SUMS)
 echo "READY $variant"
done

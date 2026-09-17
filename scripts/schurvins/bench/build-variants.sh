#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$(cd -- "$here/../.." && pwd)
work="$sdk/install/schurvins-gcc11-static"
out="$work/opt-binaries"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
# Uses dependencies prepared by build.sh. Keep the production core build intact.
for variant in base half fast zmssd all; do
  half=OFF; zmssd=OFF; fast=OFF
  case "$variant" in
    half) half=ON;; zmssd) zmssd=ON;; fast) fast=ON;; all) half=ON; zmssd=ON; fast=ON;;
  esac
  # Reuse objects with identical flags; ZMSSD's header macro requires a separate tree.
  tree="$work/opt-scalar"
  [[ $zmssd == ON ]] && tree="$work/opt-all"
  cmake -S "$here" -B "$tree" \
    -DCMAKE_TOOLCHAIN_FILE="$here/a53-gcc11-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$work/stage" \
    -DSCHUR_STATIC=ON -DSCHUR_BUILD_BENCHMARKS=ON \
    -DSCHUR_OPT_HALFSAMPLE_NEON="$half" -DSCHUR_OPT_ZMSSD_NEON="$zmssd" -DSCHUR_OPT_FAST10_NEON="$fast" -DSCHUR_OPT_ALIGN2D_NEON=OFF \
    -DSCHUR_OPT_ALIGN2D_HESSIAN_NEON=OFF -DSCHUR_OPT_WARP_AFFINE_NEON=OFF
  cmake --build "$tree" -j"${JOBS:-4}"
  mkdir -p "$out/$variant"
  for program in stereo kernel_bench; do
    "${cross}strip" -o "$out/$variant/schurvins_$program" "$tree/schurvins_$program"
  done
  printf 'HALFSAMPLE=%s\nZMSSD=%s\nFAST10=%s\nSOURCE=%s\n' "$half" "$zmssd" "$fast" \
    "$(git -C "$sdk/SchurVINS" rev-parse HEAD)" > "$out/$variant/BUILD-INFO"
  (cd "$out/$variant" && sha256sum schurvins_* > SHA256SUMS)
  echo "VARIANT READY: $variant"
done

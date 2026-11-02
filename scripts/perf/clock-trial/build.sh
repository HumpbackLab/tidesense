#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
kernel="$sdk/linux_5.10/build/sg2002_tidesense_arm64_sd"
output="$sdk/install/tidesense-clock-trial"
cross="$sdk/host-tools/gcc/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-"
[[ -f "$kernel/Module.symvers" ]] || { echo 'Build the matching TideSense kernel first' >&2; exit 1; }
mkdir -p "$output"
cp "$here/Makefile" "$here/tide_clock_trial.c" "$output/"
make -C "$kernel" M="$output" ARCH=arm64 CROSS_COMPILE="$cross" modules -j"${JOBS:-4}"
printf 'Built %s/tide_clock_trial.ko\n' "$output"

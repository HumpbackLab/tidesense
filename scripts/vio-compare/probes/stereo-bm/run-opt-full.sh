#!/bin/sh
set -eu
base=${1:?Usage: run-opt-full.sh BOARD_DIRECTORY SIZE VARIANTS...}
size=${2:?}
shift 2
nd=32
if [ "$size" = 640x400 ]; then nd=64; fi
input=/mnt/data/openvins-imu-bench-20261012-084638/input
mkdir -p "$base/full-results" /tmp/stereo-bm-opt-full
for name in "$@"; do
  spec="$size-$name"
  printf 'START_FULL %s\n' "$spec"
  awk '$1=="clk_a53" {print}' /sys/kernel/debug/clk/clk_summary
  cat /sys/class/thermal/thermal_zone0/temp
  STEREO_BM_HASHES="/tmp/stereo-bm-opt-full/$spec-hashes.csv" \
    "$base/bin/$name" "$input" "$base/maps-$size.bin" "$nd" 15 \
    "/tmp/stereo-bm-opt-full/$spec-timing.csv" "/tmp/stereo-bm-opt-full/$spec" \
    >"/tmp/stereo-bm-opt-full/$spec-run.log" 2>&1
  cat "/tmp/stereo-bm-opt-full/$spec-run.log"
  cp /tmp/stereo-bm-opt-full/"$spec"* "$base/full-results/"
  rm /tmp/stereo-bm-opt-full/"$spec"*
  if cmp -s "$base/full-results/$size-base-hashes.csv" "$base/full-results/$spec-hashes.csv"; then
    printf 'FULL_HASH_MATCH %s\n' "$spec"
  else
    printf 'FULL_HASH_DIFFERENCE %s\n' "$spec"
  fi
  printf 'FINISHED_FULL %s\n' "$spec"
done

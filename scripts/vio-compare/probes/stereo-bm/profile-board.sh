#!/bin/sh
set -eu
base=${1:-/mnt/data/stereo-bm-bench-20261012}
prof="$base/perf-20261013"
input=${2:-/mnt/data/openvins-imu-bench-20261012-084638/input}
mkdir -p "$prof" /tmp/stereo-bm-perf
for spec in 320x200-32-15 640x400-64-15; do
  size=${spec%%-*}
  rest=${spec#*-}
  nd=${rest%%-*}
  block=${rest#*-}
  printf 'START %s\n' "$spec"
  awk '$1=="clk_a53" {print}' /sys/kernel/debug/clk/clk_summary
  cat /sys/class/thermal/thermal_zone0/temp
  STEREO_BM_INTERVALS="/tmp/stereo-bm-perf/$spec-intervals.csv" \
    perf record --clockid mono -e cycles:u,L1-dcache-load-misses:u -F 499 \
      -o "/tmp/stereo-bm-perf/$spec-perf.data" -- \
      "$prof/stereo_bm" "$input" "$base/maps-$size.bin" "$nd" "$block" \
      "/tmp/stereo-bm-perf/$spec-timing.csv" "/tmp/stereo-bm-perf/$spec" \
      >"/tmp/stereo-bm-perf/$spec-run.log" 2>&1
  cat "/tmp/stereo-bm-perf/$spec-run.log"
  cp /tmp/stereo-bm-perf/"$spec"* "$prof/"
  rm /tmp/stereo-bm-perf/"$spec"*
  printf 'FINISHED %s\n' "$spec"
done
printf 'ALL_DONE\n'

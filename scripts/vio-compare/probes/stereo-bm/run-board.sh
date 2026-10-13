#!/bin/sh
set -eu
base=${1:-/mnt/data/stereo-bm-bench-20261012}
input=${2:-/mnt/data/openvins-imu-bench-20261012-084638/input}
mkdir -p "$base/results" /tmp/stereo-bm-results
awk '$1=="clk_a53" {print}' /sys/kernel/debug/clk/clk_summary
cat /proc/meminfo
for spec in 320x200-32-15 640x400-64-15 320x200-64-15; do
  size=${spec%%-*}
  rest=${spec#*-}
  nd=${rest%%-*}
  rest=${rest#*-}
  block=${rest%%-*}
  printf 'START %s\n' "$spec"
  awk '$1=="clk_a53" {print}' /sys/kernel/debug/clk/clk_summary
  cat /sys/class/thermal/thermal_zone0/temp
  "$base/stereo_bm" "$input" "$base/maps-$size.bin" "$nd" "$block" \
    "/tmp/stereo-bm-results/$spec.csv" "/tmp/stereo-bm-results/$spec" \
    >"/tmp/stereo-bm-results/$spec.log" 2>&1
  cat "/tmp/stereo-bm-results/$spec.log"
  cp /tmp/stereo-bm-results/"$spec"* "$base/results/"
  rm /tmp/stereo-bm-results/"$spec"*
  printf 'FINISHED %s\n' "$spec"
done
printf 'ALL_DONE\n'

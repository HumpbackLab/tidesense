#!/bin/sh
set -eu
base=${1:-/mnt/data/stereo-bm-opt-20261013}
mkdir -p "$base/results" /tmp/stereo-bm-opt
for size in 320x200 640x400; do
  nd=32
  if [ "$size" = 640x400 ]; then nd=64; fi
  for name in base stripe32 wide recompute stripe64 wide-recompute all32 all64 base-repeat; do
    exe=$name
    if [ "$name" = base-repeat ]; then exe=base; fi
    spec="$size-$name"
    printf 'START %s\n' "$spec"
    awk '$1=="clk_a53" {print}' /sys/kernel/debug/clk/clk_summary
    cat /sys/class/thermal/thermal_zone0/temp
    STEREO_BM_PRELOAD=1 STEREO_BM_REPEAT=3 \
      STEREO_BM_HASHES="/tmp/stereo-bm-opt/$spec-hashes.csv" \
      "$base/bin/$exe" "$base/input" "$base/maps-$size.bin" "$nd" 15 \
      "/tmp/stereo-bm-opt/$spec-timing.csv" "/tmp/stereo-bm-opt/$spec" \
      >"/tmp/stereo-bm-opt/$spec-run.log" 2>&1
    cat "/tmp/stereo-bm-opt/$spec-run.log"
    cp /tmp/stereo-bm-opt/"$spec"* "$base/results/"
    rm /tmp/stereo-bm-opt/"$spec"*
    if cmp -s "$base/results/$size-base-hashes.csv" "$base/results/$spec-hashes.csv"; then
      printf 'HASH_MATCH %s\n' "$spec"
    else
      printf 'HASH_DIFFERENCE %s\n' "$spec"
    fi
    printf 'FINISHED %s\n' "$spec"
  done
done
printf 'ALL_DONE\n'

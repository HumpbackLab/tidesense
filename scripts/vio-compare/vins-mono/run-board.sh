#!/bin/sh
set -eu
dir=/mnt/data/vio-compare/vins-mono
record=/mnt/data/vio-record-20260924-121320
mkdir -p "$dir"
flock /mnt/data/vio-compare/run.lock \
  /usr/bin/time -v -o "$dir/timing.txt" \
  "$dir/vins-mono-offline" "$record" "$dir/trajectory.csv" \
  > "$dir/run.log" 2>&1

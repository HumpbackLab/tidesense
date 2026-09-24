#!/bin/sh
set -u
work=/mnt/data/vio-compare/vins-fusion
record=/mnt/data/vio-record-20260924-121320
cd "$work" || exit 1
flock /mnt/data/vio-compare/run.lock \
  /usr/bin/time -v -o "$work/timing.txt" \
  "$work/vins_fusion_offline" "$work/fusion.yaml" "$record" "$work" \
  > "$work/run.log" 2>&1
result=$?
printf '%s\n' "$result" > "$work/exit-code.txt"
exit "$result"

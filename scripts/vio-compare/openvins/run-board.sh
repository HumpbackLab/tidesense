#!/bin/sh
set -eu
dir=/mnt/data/vio-compare/openvins
record=/mnt/data/vio-record-20260924-121320
limit=${1:-0}
flock /mnt/data/vio-compare/run.lock "$dir/run_record" \
  "$dir/config/estimator_config.yaml" "$record" \
  "$dir/trajectory-$limit.csv" "$limit" > "$dir/run-$limit.log" 2>&1

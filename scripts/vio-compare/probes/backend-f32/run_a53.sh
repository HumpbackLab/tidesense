#!/bin/sh
set -eu
root=${1:-/mnt/data/vio-compare/backend-f32}
name=${2:-mild}
record=${3:-/mnt/data/vio-record-20260924-121320}
frames=${4:-744}
modes=${5:-"original mixed fp32"}
cd "$root"
mkdir -p "results-$name"
for mode in $modes; do
  runner=./run_record.mixed
  ma_mode=original
  if [ "$mode" = mixed ]; then ma_mode=packed_float; fi
  if [ "$mode" = fp32 ]; then runner=./run_record.fp32; fi
  if [ "$mode" = fp32packed ]; then runner=./run_record.fp32packed; fi
  packed=0
  if [ "$mode" = fp32packed ]; then packed=1; fi
  set +e
  env OPENVINS_EKF_MA_MODE="$ma_mode" OPENVINS_FP32_PACKED_MA="$packed" OPENVINS_NUMERIC_EIGEN=1 \
    flock /mnt/data/vio-compare/run.lock "$runner" "config-$name-$mode/estimator_config.yaml" \
      "$record" "results-$name/trajectory-$mode.csv" "$frames" \
      > "results-$name/run-$mode.log" 2>&1
  code=$?
  set -e
  echo "$code" > "results-$name/exit-$mode.txt"
  echo "$name $mode exit=$code"
  tail -1 "results-$name/run-$mode.log"
done

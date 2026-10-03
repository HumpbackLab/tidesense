#!/bin/sh
set -eu
root=${1:-/mnt/data/vio-compare/backend-f32}
record=${2:-/mnt/data/vio-compare/backend-f32/uzh-input}
frames=${3:-0}
modes=${4:-"original mixed"}
cd "$root"
mkdir -p results-uzh
for mode in $modes; do
  ma_mode=original
  runner=./run_fisheye.mixed
  packed=0
  case "$mode" in
    original) ;;
    mixed) ma_mode=packed_float ;;
    fp32) runner=./run_fisheye.fp32 ;;
    fp32packed) runner=./run_fisheye.fp32; packed=1 ;;
    *) echo "Unknown replay mode: $mode" >&2; exit 2 ;;
  esac
  set +e
  env OPENVINS_EKF_MA_MODE="$ma_mode" OPENVINS_FP32_PACKED_MA="$packed" OPENVINS_NUMERIC_EIGEN=1 \
    flock /mnt/data/vio-compare/run.lock "$runner" \
      "config-uzh-$mode/estimator_config.yaml" "$record" \
      "results-uzh/trajectory-$mode.csv" "$frames" \
      > "results-uzh/run-$mode.log" 2>&1
  code=$?
  set -e
  echo "$code" > "results-uzh/exit-$mode.txt"
  echo "uzh $mode exit=$code"
  tail -1 "results-uzh/run-$mode.log"
done

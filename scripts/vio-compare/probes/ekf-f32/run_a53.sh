#!/bin/sh
set -eu

root=${1:-/mnt/data/vio-compare/openvins}
record=${2:-/mnt/data/vio-record-20260924-121320}
frames=${3:-550}
modes=${4:-"original packed_double packed_float"}
cd "$root"

for mode in $modes; do
  env OPENVINS_EKF_MA_MODE="$mode" OPENVINS_EKF_MA_PROBE=1 \
    flock /mnt/data/vio-compare/run.lock ./run_record.ekf-f32 \
      ./config/estimator_config.yaml "$record" "trajectory-ekf-$mode.csv" "$frames" \
      > "ekf-$mode.log" 2>&1
  tail -1 "ekf-$mode.log"
done

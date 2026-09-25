#!/bin/sh
set -eu

repo=${1:-/home/ncer/sophpi}
base="$repo/install/fisheye-benchmark/openvins"
out="$base/lk-detail-uzh"
mkdir -p "$out"

run_one() {
  name=$1
  imu=$2
  iterations=$3
  level=$4
  ransac=$5
  if [ "$imu" = 0 ]; then
    env -u OPENVINS_IMU_LK OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
      OPENVINS_LK_MAX_ITERS="$iterations" OPENVINS_LK_MAX_LEVEL="$level" \
      OPENVINS_RANSAC_MAX_ITERS="$ransac" \
      "$base/run_fisheye" "$base/uzhfpv_indoor/estimator_config.yaml" \
      "$base/uzh-input-shm" "$out/trajectory-$name.csv" 0 \
      > "$out/lkdetail-$name.log" 2>&1
  else
    env OPENVINS_IMU_LK=1 OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
      OPENVINS_LK_MAX_ITERS="$iterations" OPENVINS_LK_MAX_LEVEL="$level" \
      OPENVINS_RANSAC_MAX_ITERS="$ransac" \
      "$base/run_fisheye" "$base/uzhfpv_indoor/estimator_config.yaml" \
      "$base/uzh-input-shm" "$out/trajectory-$name.csv" 0 \
      > "$out/lkdetail-$name.log" 2>&1
  fi
  tail -1 "$out/lkdetail-$name.log"
}

run_one noimu-default 0 30 5 1000
run_one imu-default 1 30 5 1000
run_one imu-l3 1 30 3 1000
run_one imu-r200 1 30 5 200
run_one imu-combo 1 15 3 200
run_one noimu-combo 0 15 3 200

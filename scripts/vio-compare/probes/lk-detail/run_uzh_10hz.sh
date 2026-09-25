#!/bin/sh
set -eu

repo=${1:-/home/ncer/sophpi}
base="$repo/install/fisheye-benchmark/openvins"
out="$base/uzh-10hz"
mkdir -p "$out"

run_one() {
  name=$1
  use_imu=$2
  iterations=$3
  level=$4
  ransac=$5
  if [ "$use_imu" = 0 ]; then
    env -u OPENVINS_IMU_LK OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
      OPENVINS_LK_MAX_ITERS="$iterations" OPENVINS_LK_MAX_LEVEL="$level" \
      OPENVINS_RANSAC_MAX_ITERS="$ransac" \
      "$base/run_fisheye" "$base/uzhfpv_indoor/estimator_config.yaml" \
      "$base/uzh-input-10hz" "$out/trajectory-$name.csv" 0 \
      > "$out/run-$name.log" 2>&1
  else
    env OPENVINS_IMU_LK=1 OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
      OPENVINS_LK_MAX_ITERS="$iterations" OPENVINS_LK_MAX_LEVEL="$level" \
      OPENVINS_RANSAC_MAX_ITERS="$ransac" \
      "$base/run_fisheye" "$base/uzhfpv_indoor/estimator_config.yaml" \
      "$base/uzh-input-10hz" "$out/trajectory-$name.csv" 0 \
      > "$out/run-$name.log" 2>&1
  fi
  tail -1 "$out/run-$name.log"
}

run_one baseline 0 30 5 1000
run_one imu-default 1 30 5 1000
run_one optimized 1 30 5 200
run_one combo 1 15 3 200

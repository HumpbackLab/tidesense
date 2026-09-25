#!/bin/sh
set -eu

root=${1:-/mnt/data/vio-compare/openvins}
record=${2:-/mnt/data/vio-compare/replay-174756}
frames=${3:-800}
cd "$root"

run_one() {
  name=$1
  imu=$2
  iters=$3
  level=$4
  env OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
      OPENVINS_LK_MAX_ITERS="$iters" OPENVINS_LK_MAX_LEVEL="$level" \
      OPENVINS_IMU_LK="$imu" \
      flock /mnt/data/vio-compare/run.lock ./run_record.lkdetail \
      ./estimator_config.yaml "$record" "trajectory-$name.csv" "$frames" \
      > "lkdetail-$name.log" 2>&1
  tail -1 "lkdetail-$name.log"
}

# The predictor is enabled by presence of OPENVINS_IMU_LK, so the first run
# must omit the variable entirely.
env -u OPENVINS_IMU_LK OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
    OPENVINS_LK_MAX_ITERS=30 OPENVINS_LK_MAX_LEVEL=5 \
    flock /mnt/data/vio-compare/run.lock ./run_record.lkdetail \
    ./estimator_config.yaml "$record" trajectory-noimu-30-l5.csv "$frames" \
    > lkdetail-noimu-30-l5.log 2>&1
tail -1 lkdetail-noimu-30-l5.log

run_one imu-30-l5 1 30 5
run_one imu-15-l5 1 15 5
run_one imu-30-l3 1 30 3
run_one imu-15-l3 1 15 3

if [ -x ./run_record.ransac ]; then
  for spec in 'imu-ransac200 30 5' 'imu-15-l3-r200 15 3'; do
    set -- $spec
    name=$1
    iters=$2
    level=$3
    env OPENVINS_IMU_LK=1 OPENVINS_LK_PROBE=1 OPENVINS_LK_DETAIL=1 \
        OPENVINS_LK_MAX_ITERS="$iters" OPENVINS_LK_MAX_LEVEL="$level" \
        OPENVINS_RANSAC_MAX_ITERS=200 \
        flock /mnt/data/vio-compare/run.lock ./run_record.ransac \
        ./estimator_config.yaml "$record" "trajectory-$name.csv" "$frames" \
        > "lkdetail-$name.log" 2>&1
    tail -1 "lkdetail-$name.log"
  done
fi

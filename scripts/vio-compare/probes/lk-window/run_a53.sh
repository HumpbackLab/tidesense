#!/bin/sh
# Run full replays in opposite order to check timing repeatability.
set -eu
root=${1:-/mnt/data/vio-compare/lk-window-mixed}
record=${2:-/mnt/data/vio-compare/backend-f32/uzh-input}
cd "$root"
mkdir -p results-window
unset OPENVINS_NUMERIC_EIGEN OPENVINS_CAMERA_PERF_GATE
for trial in 1 2; do
  windows="15x15 16x15 16x16"
  [ "$trial" = 1 ] || windows="16x16 16x15 15x15"
  for window in $windows; do
    name="$window-r$trial"
    date -Iseconds > "results-window/start-$name.txt"
    grep clk_a53 /sys/kernel/debug/clk/clk_summary > "results-window/clock-$name.txt"
    set +e
    env OPENVINS_EKF_MA_MODE=packed_float OPENVINS_FP32_PACKED_MA=1 \
      flock /mnt/data/vio-compare/run.lock \
      "./run_fisheye.$window" "config-window-$name/estimator_config.yaml" \
      "$record" "results-window/trajectory-$name.csv" 0 \
      > "results-window/run-$name.log" 2>&1
    code=$?
    set -e
    echo "$code" > "results-window/exit-$name.txt"
    echo "$name exit=$code"
    tail -1 "results-window/run-$name.log"
    [ "$code" = 0 ] || exit "$code"
  done
done

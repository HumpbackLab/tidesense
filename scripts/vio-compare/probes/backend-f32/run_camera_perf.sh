#!/bin/sh
set -eu
root=${1:-/mnt/data/vio-compare/backend-f32}
frames=${2:-0}
start=${3:-200}
name=${4:-perf-uzh-camera}
cd "$root"
mkdir -p results-uzh
control="$root/$name.ctl"
ack="$root/$name.ack"
if [ -e "$control" ] || [ -e "$ack" ]; then
  echo "Perf control FIFOs already exist: $name" >&2
  exit 2
fi
mkfifo "$control" "$ack"
exec 3<> "$control"
exec 4<> "$ack"
trap 'unlink "$control"; unlink "$ack"' EXIT
unset OPENVINS_NUMERIC_EIGEN
export OPENVINS_FP32_PACKED_MA=1
export OPENVINS_CAMERA_PERF_GATE=1
export OPENVINS_CAMERA_PERF_START="$start"
export OPENVINS_CAMERA_PERF_CTL="$control"
export OPENVINS_CAMERA_PERF_ACK="$ack"
set +e
flock /mnt/data/vio-compare/run.lock perf record -e cycles:u -F 99 -g --call-graph fp \
  -D -1 -N --control="fifo:$control,$ack" -o "$name.data" -- \
  ./run_fisheye.perf config-uzh-perf/estimator_config.yaml uzh-input \
  "results-uzh/trajectory-$name.csv" "$frames" > "$name.log" 2>&1
code=$?
set -e
echo "$code" > "$name.exit"
echo "$name exit=$code"
tail -5 "$name.log"
exit "$code"

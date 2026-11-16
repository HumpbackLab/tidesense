#!/bin/sh
# Install alongside the experiment's run.sh, or set SCHUR_LOOP_APP explicitly.
set -eu
app=${SCHUR_LOOP_APP:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
if [ "$#" -eq 0 ]; then
  echo 'Usage: run-ive-tuned.sh --vio CALIB.yaml auto POSES.csv [FRAMES]' >&2
  exit 2
fi
export SCHUR_LOOP_CLOSING=1
export SCHUR_ORB_DETECTOR=ive-st
# Calibrated against FAST on this board's 640x400 images; scene dependent.
export SCHUR_ORB_IVE_QUALITY="${SCHUR_ORB_IVE_QUALITY:-3}"
export SCHUR_LOOP_MATCH_BACKEND="${SCHUR_LOOP_MATCH_BACKEND:-cpu-batch}"
export SCHUR_LOOP_REUSE_ORB="${SCHUR_LOOP_REUSE_ORB:-1}"
export SCHUR_LOOP_PRECHECK_3D="${SCHUR_LOOP_PRECHECK_3D:-1}"
export SCHUR_LOOP_NICE="${SCHUR_LOOP_NICE:-10}"
export SCHUR_ROS="${SCHUR_ROS:-0}"
export SCHUR_PREDICTION="${SCHUR_PREDICTION:-0}"
exec "$app/run.sh" "$@"

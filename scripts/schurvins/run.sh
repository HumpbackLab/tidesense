#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# Retained estimator fixes; explicit environment overrides remain available.
export SCHUR_IMU_MIDPOINT="${SCHUR_IMU_MIDPOINT:-1}"
export SCHUR_PIXEL_RESIDUAL="${SCHUR_PIXEL_RESIDUAL:-1}"
export SCHUR_REGISTER_STEREO_POINTS="${SCHUR_REGISTER_STEREO_POINTS:-1}"
if [ -d "$app_dir/lib" ]; then
  export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
case "${1:-}" in
  --vio) imu_device=${3:-} ;;
  --vio-smoke) imu_device=${2:-} ;;
  --imu-test) imu_device=${2:-} ;;
  *) imu_device= ;;
esac
if [ "$imu_device" = auto ]; then
  SCHUR_IMU_DEVICE=$("$app_dir/bin/prepare-imu.sh")
  export SCHUR_IMU_DEVICE
fi
if [ "${1:-}" = --imu-test ]; then
  shift
  exec "$app_dir/bin/schurvins_imu_test" "$@"
fi
if [ -x "$app_dir/lib/ld-linux-aarch64.so.1" ]; then
  exec "$app_dir/lib/ld-linux-aarch64.so.1" --library-path "$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$app_dir/bin/schurvins_stereo" "$@"
fi
exec "$app_dir/bin/schurvins_stereo" "$@"

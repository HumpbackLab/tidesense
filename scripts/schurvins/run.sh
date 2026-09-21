#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -d "$app_dir/lib" ]; then
  export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
case "${1:-}" in
  --vio) imu_device=${4:-} ;;
  --vio-smoke) imu_device=${3:-} ;;
  --calibrate)
    imu_device=
    previous=
    for option in "$@"; do
      if [ "$previous" = --imu ]; then imu_device=$option; fi
      previous=$option
    done
    ;;
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
exec "$app_dir/bin/schurvins_stereo" "$@"

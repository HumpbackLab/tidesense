#!/bin/sh
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" = --help ]; then exec "$app/bin/openvins_live" --help; fi
if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo 'Usage: run-live.sh CONFIG.yaml SENSOR.ini auto|/dev/iio:deviceN POSES.csv [FRAMES]' >&2
  exit 2
fi
if [ "$3" = auto ]; then
  SCHUR_IMU_DEVICE=$("$app/bin/prepare-imu.sh")
  export SCHUR_IMU_DEVICE
fi
export OPENVINS_EKF_MA_MODE=original
exec "$app/bin/openvins_live" "$@"

#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export SCHUR_IMU_MIDPOINT="${SCHUR_IMU_MIDPOINT:-1}"
export SCHUR_PIXEL_RESIDUAL="${SCHUR_PIXEL_RESIDUAL:-1}"
export SCHUR_REGISTER_STEREO_POINTS="${SCHUR_REGISTER_STEREO_POINTS:-1}"
for option in "$@"; do
  if [ "$option" = --help ]; then exec "$here/bin/tidesense_vio_depth" "$@"; fi
done
# LiveImu resolves 'auto' through the same helper as the standalone VIO mode.
export SCHUR_IMU_DEVICE="${SCHUR_IMU_DEVICE:-$("$here/bin/prepare-imu.sh")}"
exec "$here/bin/tidesense_vio_depth" "$@"

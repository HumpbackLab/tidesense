#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" = --help ]; then exec "$here/bin/tidesense_record" "$@"; fi
imu=auto
previous=
for option in "$@"; do
 if [ "$previous" = --imu ]; then imu=$option; fi
 previous=$option
done
if [ "$imu" = auto ]; then
 SCHUR_IMU_DEVICE=$("$here/bin/prepare-imu.sh")
 export SCHUR_IMU_DEVICE
fi
exec "$here/bin/tidesense_record" "$@"

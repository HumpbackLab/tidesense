#!/bin/sh
# Shared OV9281 FSIN: SC7U22 hard IRQ / 10, nominally 20 Hz.
set -eu
lock=/run/tidesense-fsin.lock
mkdir "$lock" 2>/dev/null || { echo 'FSIN control is busy' >&2; exit 1; }
trap 'rmdir "$lock"' EXIT
fail() { echo "$*" >&2; exit 1; }
iio=
for dev in /sys/bus/iio/devices/iio:device*; do
 [ -f "$dev/name" ] || continue
 if [ "$(cat "$dev/name")" = sc7u22 ] && [ -f "$dev/fsin_enable" ]; then iio=$dev; break; fi
done
[ -n "$iio" ] || fail 'SC7U22 kernel FSIN support missing; install the matching TideSense kernel'
prepare() {
 [ "$(cat "$iio/fsin_enable")" = 0 ] || fail 'Stop FSIN before prepare'
 echo 0 > "$iio/fsin_enable"
 echo 0 > "$iio/fsin_pulse_limit"
 echo 'FSIN prepared: kernel owns PWR_GPIO0, output low'
}
case "${1:-}" in
 prepare) [ "$#" -eq 1 ] || fail 'prepare takes no arguments'; prepare ;;
 start|train)
  action=$1
  [ "${2:-20}" = 20 ] || fail 'FSIN is fixed at IMU 200 Hz / 10 (20 Hz)'
  [ "$(cat "$iio/fsin_enable")" = 0 ] || fail 'Stop FSIN before starting a new pulse train'
  count=0
  if [ "$action" = train ]; then
   [ "$#" -eq 3 ] || fail 'Usage: fsin.sh train 20 COUNT'
   count=$3
   case "$count" in ''|0*|*[!0-9]*) fail 'COUNT must be 1..10000000' ;; esac
   [ "$count" -ge 1 ] && [ "$count" -le 10000000 ] || fail 'COUNT must be 1..10000000'
  else
   [ "$#" -le 2 ] || fail 'Usage: fsin.sh start [20]'
  fi
  echo "$count" > "$iio/fsin_pulse_limit"
  echo 1 > "$iio/fsin_enable"
  echo "FSIN $action: IMU INT1 / 10, 100 us high, pulse limit $count (0=continuous)"
  ;;
 stop)
  [ "$#" -eq 1 ] || fail 'stop takes no arguments'
  echo 0 > "$iio/fsin_enable"
  echo 'FSIN stopped: PWR_GPIO0 low'
  ;;
 status) cat "$iio/fsin_diagnostics" ;;
 *) fail 'Usage: fsin.sh prepare | start [20] | train 20 COUNT | stop | status' ;;
esac

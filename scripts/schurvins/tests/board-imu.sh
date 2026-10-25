#!/bin/sh
# Run on TideSense with the board stationary and no other camera/IMU application.
set -eu
app=${1:-/mnt/data/schurvins-gcc11-static}
logs=${2:-/tmp/schurvins-imu-test}
mkdir -p "$logs"
active_pid=
trap 'if [ -n "$active_pid" ]; then kill -TERM "$active_pid" 2>/dev/null || :; wait "$active_pid" 2>/dev/null || :; fi' EXIT INT TERM
"$app/run.sh" --imu-test auto 10 > "$logs/imu.log" 2>&1 &
active_pid=$!
sleep 1
if "$app/run.sh" --imu-test auto 1 > "$logs/exclusive.log" 2>&1; then
    echo 'Concurrent IMU reader was not rejected' >&2; exit 1
fi
grep -Eq 'owned by another process|Device or resource busy' "$logs/exclusive.log"
wait "$active_pid"
active_pid=
"$app/run.sh" --self-test "$app/synthetic.yaml" > "$logs/self.log" 2>&1
"$app/run.sh" --camera-smoke 30 > "$logs/visual.log" 2>&1
"$app/run.sh" --vio-smoke auto 120 > "$logs/vio.log" 2>&1
SCHUR_TEST_PROCESS_DELAY_MS=100 "$app/run.sh" --vio-smoke auto 40 > "$logs/overload.log" 2>&1
"$app/run.sh" --vio-smoke auto 0 > "$logs/interrupt.log" 2>&1 &
active_pid=$!
sleep 6
kill -INT "$active_pid"
wait "$active_pid"
active_pid=
if "$app/run.sh" --vio "$app/camera-smoke.yaml" auto "$logs/rejected-poses.csv" 1 > "$logs/calibration.log" 2>&1; then
    echo 'Test calibration accepted in production mode' >&2; exit 1
fi
grep -q 'Use real calibration' "$logs/calibration.log"
if "$app/run.sh" --vio-smoke /dev/nonexistent-sc7u22 1 > "$logs/missing-imu.log" 2>&1; then
    echo 'Missing IMU was accepted' >&2; exit 1
fi
grep -q 'Cannot open IMU' "$logs/missing-imu.log"
imu_device=$("$app/bin/prepare-imu.sh")
iio_sys=/sys/bus/iio/devices/${imu_device##*/}
cat "$iio_sys/diagnostics" > "$logs/iio-diagnostics-final.log"
[ "$(cat "$iio_sys/buffer/enable")" = 0 ] || { echo 'IIO buffer left enabled' >&2; exit 1; }
cat "$iio_sys/fsin_diagnostics" > "$logs/fsin-final.log"
grep -q '^enable=0$' "$logs/fsin-final.log"
for comm in /proc/[0-9]*/comm; do
    [ -f "$comm" ] || continue
    case "$(cat "$comm" 2>/dev/null)" in
        spi0) pid=${comm#/proc/}; pid=${pid%/comm}; chrt -p "$pid" > "$logs/spi-policy-final.log" ;;
    esac
done
echo "IMU BOARD TEST PASS: $logs"

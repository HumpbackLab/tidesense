#!/bin/sh
set -eu
tag=${1:?usage: bench-board.sh TAG LOOP_0_OR_1 FRAMES}
loop=${2:?}
frames=${3:-600}
case "$tag" in *[!a-zA-Z0-9_-]*|'') echo 'Invalid tag' >&2; exit 2 ;; esac
case "$loop" in 0|1) ;; *) exit 2 ;; esac
app=${SCHUR_LOOP_APP:-/mnt/data/schur-loop-closing}
calibration=${SCHUR_LOOP_CALIBRATION:-$app/schurvins.yaml}
out=${SCHUR_LOOP_BENCH:-/tmp/schur-loop-bench}
mkdir -p "$out"
prefix="$out/$tag"
[ ! -e "$prefix.log" ] || { echo "Already exists: $prefix.log" >&2; exit 1; }
lock=/tmp/tidesense-schur-loop-benchmark.lock
mkdir "$lock" 2>/dev/null || { echo 'Another benchmark holds the board lock' >&2; exit 1; }
trap 'rmdir "$lock"' EXIT
cat /proc/uptime >"$prefix.start"
if [ -n "${SCHUR_LOOP_REPLAY:-}" ]; then
  set -- --replay "$SCHUR_LOOP_REPLAY" "$prefix.poses.csv"
else
  set -- --vio "$calibration" auto "$prefix.poses.csv" "$frames"
fi
SCHUR_ROS=0 SCHUR_PREDICTION=0 SCHUR_LOOP_CLOSING="$loop" \
  SCHUR_LOOP_CSV="$prefix.loop.csv" SCHUR_TIMING_CSV="$prefix.timing.csv" \
  SCHUR_PROFILE_CSV="$prefix.profile.csv" \
  "$app/run.sh" "$@" \
  >"$prefix.log" 2>&1 &
app_pid=$!
trap 'kill -INT "$app_pid" 2>/dev/null || true' INT TERM
trap 'kill -INT "$app_pid" 2>/dev/null || true; rmdir "$lock"' EXIT
printf 'uptime_s,user_ticks,sys_ticks,rss_kib,hwm_kib,total_ticks,idle_ticks,available_kib\n' >"$prefix.resources.csv"
while kill -0 "$app_pid" 2>/dev/null; do
  if [ -r "/proc/$app_pid/stat" ]; then
    available=$(awk '/MemAvailable:/ {print $2}' /proc/meminfo)
    if [ "$available" -lt 16384 ]; then
      echo 'Stopped: less than 16 MiB available memory' >"$prefix.memory-stop"
      kill -INT "$app_pid" 2>/dev/null || true
    fi
    awk '{printf "%s,", $1}' /proc/uptime >>"$prefix.resources.csv"
    awk '{printf "%s,%s,", $14,$15}' "/proc/$app_pid/stat" >>"$prefix.resources.csv" 2>/dev/null || true
    awk '/VmRSS:/ {rss=$2} /VmHWM:/ {hwm=$2} END {printf "%d,%d,",rss,hwm}' "/proc/$app_pid/status" >>"$prefix.resources.csv" 2>/dev/null || true
    awk '/^cpu / {s=0; for(i=2;i<=9;++i)s+=$i; printf "%d,%d,",s,$5}' /proc/stat >>"$prefix.resources.csv"
    printf '%s\n' "$available" >>"$prefix.resources.csv"
  fi
  sleep 1
done
set +e
wait "$app_pid"
result=$?
set -e
trap - INT TERM
cat /proc/uptime >"$prefix.end"
printf '%s\n' "$result" >"$prefix.exit"
tail -18 "$prefix.log"
exit "$result"

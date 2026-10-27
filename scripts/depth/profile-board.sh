#!/bin/sh
# Profile a separate build, preserving the installed application.
set -eu
base=${1:?Usage: profile-board.sh PROFILE_DIR WIDTH LABEL [SECONDS]}
width=${2:?width required}
label=${3:?label required}
seconds=${4:-18}
case "$width" in 320|640) ;; *) exit 2 ;; esac
case "$label" in ''|*[!a-zA-Z0-9_-]*) exit 2 ;; esac
package=${DEPTH_PACKAGE:-/mnt/data/tidesense-depth}
tmp=/tmp/tidesense-depth-perf-$$
mkdir -p "$tmp" "$base/$label"
app_pid=
cleanup() {
  if [ -n "$app_pid" ]; then kill -TERM "$app_pid" 2>/dev/null || true; fi
  if [ -n "$app_pid" ]; then wait "$app_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
TIDESENSE_DEPTH_PROFILE="$tmp/stages.csv" "$base/tidesense_depth" \
  --calibration "$package/calibration.yaml" --width "$width" --fps 20 \
  >"$tmp/run.log" 2>&1 &
app_pid=$!
echo "$app_pid" > "$tmp/app.pid"
attempt=0
while ! grep -q 'camera: first pair' "$tmp/run.log"; do
  kill -0 "$app_pid" || { cat "$tmp/run.log"; exit 1; }
  attempt=$((attempt+1))
  [ "$attempt" -le 20 ] || { cat "$tmp/run.log"; exit 1; }
  sleep 1
done
sleep 2
snapshot() {
  cat /proc/uptime /proc/stat
  for tid in /proc/"$app_pid"/task/*; do cat "$tid/stat"; done
  for stat in /proc/[0-9]*/stat; do
    read -r child command state parent rest < "$stat" 2>/dev/null || continue
    if [ "$parent" = "$app_pid" ]; then cat "$stat" 2>/dev/null || true; fi
  done
  awk '$1=="clk_a53" || $1=="clk_mpll" {print}' /sys/kernel/debug/clk/clk_summary
  cat /sys/class/thermal/thermal_zone0/temp
}
snapshot > "$tmp/before.txt"
if [ "${DEPTH_PERF_SYSTEM:-0}" = 1 ]; then
  set -- -a
  events=cycles
  cat /proc/kallsyms > "$tmp/kallsyms"
else
  set -- -p "$app_pid"
  events=cycles:u,L1-dcache-load-misses:u
fi
echo "PROFILE_BEGIN pid=$app_pid"
perf stat -t "$app_pid" -e task-clock,cycles:u,instructions:u,L1-dcache-load-misses:u,context-switches \
  -o "$tmp/stat.txt" -- perf record -N --clockid mono \
  -e "$events" -F 499 -g --call-graph fp \
  "$@" -o "$tmp/perf.data" -- sleep "$seconds" >"$tmp/record.log" 2>&1
echo PROFILE_END
snapshot > "$tmp/after.txt"
kill -TERM "$app_pid"
wait "$app_pid"; app_pid=
cp "$tmp/"* "$base/$label/"
rm -r "$tmp"
cat "$base/$label/record.log" "$base/$label/stat.txt"
tail -5 "$base/$label/run.log"

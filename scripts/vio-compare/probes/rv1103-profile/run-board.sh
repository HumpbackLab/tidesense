#!/bin/sh
# Deploy into /root/openvins-profile-20261025, then run there.
set -eu
cd "${OPENVINS_PROFILE_DIR:-/root/openvins-profile-20261025}"
mode=${1:-baseline}
name=${2:-$mode}
samples=${3:-400}
mkdir -p results
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1
export OPENVINS_DETAIL_CSV="results/$name-detail.csv"
{
    uname -a
    cat /proc/cmdline
    cat /proc/cpuinfo
    cat /proc/meminfo
    cat /proc/vmstat
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
    cat /sys/class/thermal/thermal_zone0/temp
    sha256sum bin/openvins_mono share/openvins/euroc_mav/*
    if [ -n "${OPENVINS_CONFIG_DIR:-}" ]; then sha256sum "$OPENVINS_CONFIG_DIR"/*; fi
    ps
} > "results/$name-before.txt"
set -- bin/openvins_mono --dataset dataset --profile mono --native-init \
    --samples "$samples" --warmup 5 --max-slam 0 --opencv-threads 1 \
    --trajectory "results/$name-poses.csv" --stage-timing "results/$name-stages.csv" --json
if [ -n "${OPENVINS_CONFIG_DIR:-}" ]; then set -- "$@" --config-dir "$OPENVINS_CONFIG_DIR"; fi
case "$mode" in
  baseline) /usr/bin/time -v "$@" > "results/$name.json" 2> "results/$name.log" ;;
  perf) /usr/bin/time -v perf record --clockid mono -e cycles:u -F 99 -m 64 -B \
      -o "results/$name.data" -- "$@" > "results/$name.json" 2> "results/$name.log" ;;
  *) echo "usage: $0 baseline|perf [name] [samples]" >&2; exit 2 ;;
esac
{
    cat /proc/meminfo
    cat /proc/vmstat
    cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
    cat /sys/class/thermal/thermal_zone0/temp
    dmesg | tail -30
} > "results/$name-after.txt"
cat "results/$name.json"

#!/bin/sh
set -eu
mode=${1:-both}
name=${2:-$mode-400}
samples=${3:-400}
case "$mode" in software|software4|pyramid|lk|both) ;; *) echo 'Unknown mode' >&2;exit 2 ;; esac
base=${OPENVINS_RKIVE_DIR:-/root/rkive-test-20261025}
mkdir -p "$base/results"
governor=/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
previous_governor=$(cat "$governor")
restore_governor() { printf '%s\n' "$previous_governor" > "$governor"; }
trap restore_governor 0
trap 'exit 130' 2
trap 'exit 143' 15
echo performance > "$governor"
if [ "$mode" != software ] && [ "$mode" != software4 ]; then
    if ! grep -q '^rve ' /proc/modules; then insmod /oem/usr/ko/rve.ko; fi
fi
{
    echo "OPENVINS_RKIVE_MODE=$mode"
    echo 'HW LK: maxLevel=3, iterations=20, epsilon_q11=1, min_eig_q8=1'
    echo 'Software window=15x15; SDK hardware API has no window-size field'
    echo 'Software4: maxLevel=3, iterations=20, OpenCV epsilon=0.01 and minEigThreshold=0.0001'
    cat /proc/rve/scheduler_status 2>/dev/null || true
} > "$base/results/$name-rkive-before.txt"
OPENVINS_RKIVE_MODE=$mode OPENVINS_PROFILE_DIR=$base \
    sh "${OPENVINS_BOARD_RUNNER:-/root/openvins-profile-20261025/run-board.sh}" "${OPENVINS_RUN_MODE:-baseline}" "$name" "$samples"
{
    cat /proc/rve/scheduler_status 2>/dev/null || true
    cat /proc/rk_dma_heap/dma_heap_info
    cat /proc/meminfo
} > "$base/results/$name-rkive-after.txt"

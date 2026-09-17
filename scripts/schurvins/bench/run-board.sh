#!/usr/bin/env bash
# Run variants serially on one A53; never run benchmarks concurrently.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk=$(cd -- "$here/../.." && pwd)
binaries=${BINARIES_DIR:-"$sdk/install/schurvins-gcc11-static/opt-binaries"}
results=${RESULTS_DIR:-"$sdk/install/schurvins-gcc11-static/opt-results"}
board=${BOARD:-root@192.168.123.99}
remote=${REMOTE_DIR:-/mnt/data/schur-opt-test}
dataset=${DATASET:-/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay}
# Configure ssh keys or supply SSHPASS in the environment, never save passwords.
ssh_cmd=(ssh -o ConnectTimeout=10)
scp_cmd=(scp -O)
if [[ -n ${SSHPASS:-} ]]; then ssh_cmd=(sshpass -e "${ssh_cmd[@]}"); scp_cmd=(sshpass -e "${scp_cmd[@]}"); fi
# These paths are interpolated into remote shell commands. Accept simple paths only.
for value in "$remote" "$dataset"; do
  [[ $value =~ ^/[a-zA-Z0-9_./-]+$ ]] || { echo 'Unsafe remote path' >&2; exit 2; }
done
mkdir -p "$results"
# Existing calibration and synchronized CSV must be prepared in REMOTE_DIR.
# Alternate baseline/candidate ordering when requesting repeated runs.
for variant in "${@:-base}"; do
  [[ $variant =~ ^(base|half|zmssd|fast|all|align)$ ]] || exit 2
  tag="$variant-$(date -u +%Y%m%dT%H%M%SZ)"
  [[ -f "$binaries/$variant/SHA256SUMS" ]]
  "${scp_cmd[@]}" "$binaries/$variant/schurvins_stereo" "$binaries/$variant/schurvins_kernel_bench" "$board:$remote/"
  "${ssh_cmd[@]}" "$board" "cd '$remote' && sha256sum schurvins_kernel_bench schurvins_stereo" > "$results/$tag-SHA256SUMS"
  diff -u "$binaries/$variant/SHA256SUMS" "$results/$tag-SHA256SUMS"
  "${ssh_cmd[@]}" "$board" "cd '$remote' && cat /sys/kernel/debug/clk/clk_a53/clk_rate && cat /sys/class/thermal/thermal_zone0/temp" > "$results/$tag-environment.txt"
  ram="/tmp/schur-bench-$tag"
  echo "RUNNING $tag (logs/timings in tmpfs)"
  "${ssh_cmd[@]}" "$board" "mkdir -p '$ram' && cd '$remote' && ./schurvins_kernel_bench \$(awk -F, 'NR==1 || NR==251 || NR==501 {print \$2}' stereo.csv) > '$ram/$tag-kernels.log' 2>&1 && SCHUR_TIMING_CSV='$ram/$tag.csv' ./schurvins_stereo calib.yaml stereo.csv '$dataset/mav0/imu0/data.csv' '$ram/$tag-poses.csv' > '$ram/$tag.log' 2>&1"
  "${scp_cmd[@]}" "$board:$ram/$tag*" "$results/"
  "${ssh_cmd[@]}" "$board" "cat /sys/class/thermal/thermal_zone0/temp" >> "$results/$tag-environment.txt"
  # Host copies are retained; remove only files created by this completed run.
  "${ssh_cmd[@]}" "$board" "cd '$ram' && rm '$tag-kernels.log' '$tag.csv' '$tag-poses.csv' '$tag.log' && cd / && rmdir '$ram'"
  echo "COMPLETED $tag"
done

#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
variant=${1:?Usage: record-ram.sh base|all|align}
[[ $variant == base || $variant == all || $variant == align ]] || exit 2
bin="$sdk/install/schurvins-gcc11-static/profile/$variant"
out="$sdk/install/schurvins-gcc11-static/profile/$variant-ram"
board=${BOARD:-root@192.168.123.99}
remote=/mnt/data/schur-profile
ram=/tmp/schur-profile-$variant
dataset=/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay
ssh_cmd=(ssh -o ConnectTimeout=10); scp_cmd=(scp -O)
if [[ -n ${SSHPASS:-} ]]; then ssh_cmd=(sshpass -e "${ssh_cmd[@]}"); scp_cmd=(sshpass -e "${scp_cmd[@]}"); fi
mkdir -p "$out"
cp "$bin/symbols" "$out/symbols"
"${scp_cmd[@]}" "$bin/stereo" "$board:$remote/stereo"
"${ssh_cmd[@]}" "$board" "cd $remote && sha256sum stereo && cat /proc/mounts && cat /sys/kernel/debug/clk/clk_a53/clk_rate && cat /sys/class/thermal/thermal_zone0/temp" > "$out/board-info.txt"
echo "Recording $variant to tmpfs"
"${ssh_cmd[@]}" "$board" "mkdir -p $ram && cd $remote && SCHUR_PROFILE_CSV=$ram/intervals.csv perf record -N -F 199 -e cycles -g --call-graph fp --clockid mono -m 128 -o $ram/perf.data -- ./stereo calib.yaml stereo.csv $dataset/mav0/imu0/data.csv $ram/poses.csv > $ram/run.log 2> $ram/perf.log"
"${scp_cmd[@]}" "$board:$ram/*" "$out/"
"${ssh_cmd[@]}" "$board" "rm $ram/perf.data"
echo "Recorded $variant to tmpfs"

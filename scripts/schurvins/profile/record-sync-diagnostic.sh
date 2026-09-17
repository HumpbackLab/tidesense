#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
variant=${1:?Usage: record-sync-diagnostic.sh base|all}
[[ $variant == base || $variant == all ]] || exit 2
out="$sdk/install/schurvins-gcc11-static/profile/$variant"
board=${BOARD:-root@192.168.123.99}
remote=/mnt/data/schur-profile
dataset=/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay
ssh_cmd=(ssh -o ConnectTimeout=10); scp_cmd=(scp -O)
if [[ -n ${SSHPASS:-} ]]; then ssh_cmd=(sshpass -e "${ssh_cmd[@]}"); scp_cmd=(sshpass -e "${scp_cmd[@]}"); fi
"${scp_cmd[@]}" "$out/stereo" "$board:$remote/stereo"
"${ssh_cmd[@]}" "$board" "cd $remote && sha256sum stereo && cat /sys/kernel/debug/clk/clk_a53/clk_rate && cat /sys/class/thermal/thermal_zone0/temp" > "$out/board-info.txt"
echo "Recording $variant"
"${ssh_cmd[@]}" "$board" "cd $remote && SCHUR_PROFILE_CSV=$variant-intervals.csv perf record -N -F 199 -e cycles -g --call-graph fp --clockid mono -m 128 -o $variant.perf.data -- ./stereo calib.yaml stereo.csv $dataset/mav0/imu0/data.csv $variant-poses.csv > $variant.log 2> $variant-perf.log"
"${scp_cmd[@]}" "$board:$remote/$variant*" "$out/"
# Retain all raw data on the host, free only this script's board-side trace.
"${ssh_cmd[@]}" "$board" "rm $remote/$variant.perf.data"
echo "Recorded $variant"

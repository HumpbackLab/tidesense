#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
variant=${1:-both}
[[ $variant =~ ^(base|hessian|warp|both)$ ]] || exit 2
bin="$sdk/install/schurvins-gcc11-static/next-binaries/$variant"
out="$sdk/install/schurvins-gcc11-static/next-results"
board=${BOARD:-root@192.168.123.99}
ssh_cmd=(ssh -o ConnectTimeout=10); scp_cmd=(scp -O)
if [[ -n ${SSHPASS:-} ]]; then ssh_cmd=(sshpass -e "${ssh_cmd[@]}"); scp_cmd=(sshpass -e "${scp_cmd[@]}"); fi
mkdir -p "$out"
"${scp_cmd[@]}" "$bin/schurvins_align2d_bench" "$bin/schurvins_warp_bench" "$board:/mnt/data/schur-opt-test/"
"${ssh_cmd[@]}" "$board" 'sh -s' <<'REMOTE'
set -e
cd /mnt/data/schur-opt-test
images=$(awk -F, 'NR==1 || NR==251 || NR==501 {print $2}' stereo.csv)
./schurvins_align2d_bench $images > /tmp/schur-next-check.log 2>&1
./schurvins_warp_bench $images >> /tmp/schur-next-check.log 2>&1
REMOTE
"${scp_cmd[@]}" "$board:/tmp/schur-next-check.log" "$out/$variant-checks.log"
cat "$out/$variant-checks.log"

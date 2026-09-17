#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
bin="$sdk/install/schurvins-gcc11-static/align2d-binaries/align/schurvins_align2d_bench"
board=${BOARD:-root@192.168.123.99}
ssh_cmd=(ssh -o ConnectTimeout=10); scp_cmd=(scp -O)
if [[ -n ${SSHPASS:-} ]]; then ssh_cmd=(sshpass -e "${ssh_cmd[@]}"); scp_cmd=(sshpass -e "${scp_cmd[@]}"); fi
"${scp_cmd[@]}" "$bin" "$board:/mnt/data/schur-opt-test/"
# Fixed test paths; output goes to tmpfs, then is copied after timing is over.
"${ssh_cmd[@]}" "$board" 'sh -s' <<'REMOTE'
cd /mnt/data/schur-opt-test || exit 1
./schurvins_align2d_bench $(awk -F, 'NR==1 || NR==251 || NR==501 {print $2}' stereo.csv) > /tmp/schur-align2d-check.log 2>&1
REMOTE
"${ssh_cmd[@]}" "$board" 'cat /tmp/schur-align2d-check.log'

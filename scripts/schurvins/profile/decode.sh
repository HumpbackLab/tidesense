#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
variant=${1:?Usage: decode.sh base|all|align}
[[ $variant == base || $variant == all || $variant == align ]] || exit 2
out="$sdk/install/schurvins-gcc11-static/profile/$variant-ram"
perf_host=${PERF_HOST:-/usr/lib/linux-tools/5.15.0-191-generic/perf}
[[ -x $perf_host ]] || { echo 'Set PERF_HOST to a working host perf executable' >&2; exit 1; }
mkdir -p "$out/symfs/mnt/data/schur-profile"
ln -sf ../../../../symbols "$out/symfs/mnt/data/schur-profile/stereo"
# Optional kernel ELF must match the build ID in the trace. User ranking does not require it.
kernel_args=()
if [[ -n ${VMLINUX:-} ]]; then kernel_args=(-k "$VMLINUX"); fi
"$perf_host" script -i "$out/perf.data" --symfs "$out/symfs" "${kernel_args[@]}" \
  --ns -F time,period,event,ip,sym,dso,misc > "$out/samples.txt" 2> "$out/decode.log"
"$perf_host" report -D -i "$out/perf.data" 2>/dev/null | sed -n '/^Aggregated stats:/,$p' > "$out/event-stats.txt"
python3 "$sdk/scripts/schurvins/profile/analyze.py" "$out/samples.txt" "$out/intervals.csv" --output "$out/summary.json"

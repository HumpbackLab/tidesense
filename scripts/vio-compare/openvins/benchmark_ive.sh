#!/bin/sh
# Complete C906/TPU frontend and A53 pipeline comparison, with optional IVE.
# Write timed outputs to RAM; copy to SD only after each run.
set -eu
if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo 'Usage: benchmark_ive.sh CONFIG DATASET OUTPUT_DIR [FRAME_LIMIT=1040]' >&2
  exit 2
fi
config=$1
dataset=$2
output=$3
limit=${4:-1040}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
runner=${OPENVINS_IVE_RUNNER:-$here/run_ive.a53}
export C906_TOOLS_DIR=${C906_TOOLS_DIR:-$here/tools}
export OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_LK=1 OPENVINS_TPU_IVE=1
export OPENVINS_TPU_CPU_TAIL=0 OPENVINS_TPU_LOCK_EXEC=1 OPENVINS_EKF_PACKED_F64=1
export OPENVINS_PRELOAD_FIRST=${OPENVINS_PRELOAD_FIRST:-1000}
export OPENVINS_PRELOAD_COUNT=${OPENVINS_PRELOAD_COUNT:-40}
export OPENVINS_TPU_IVE_QUALITY=${OPENVINS_TPU_IVE_QUALITY:-16}
unset OPENVINS_BACKEND_DETAIL_FILE OPENVINS_QR7_DETAIL_FILE
tmpdir=$(mktemp -d /tmp/openvins-ive.XXXXXX)
mkdir -p "$output"
echo "IVE benchmark RAM outputs: $tmpdir"
for mode in ${OPENVINS_IVE_MODES:-off hist st both}; do
  case "$mode" in
    off) hist=0; st=0 ;;
    hist) hist=1; st=0 ;;
    st) hist=0; st=1 ;;
    both) hist=1; st=1 ;;
    *) echo "Unknown IVE mode: $mode" >&2; exit 2 ;;
  esac
  export OPENVINS_TPU_IVE_HIST=$hist OPENVINS_TPU_IVE_ST=$st
  export OPENVINS_TPU_TRACE="$tmpdir/$mode-trace.csv"
  echo "Running $mode (hist=$hist ST=$st)"
  "$here/with-tpu.sh" "$runner" "$config" "$dataset" "$tmpdir/$mode.csv" "$limit" 0 > "$tmpdir/$mode.log" 2>&1
  mv "$tmpdir/$mode.csv" "$tmpdir/$mode.log" "$tmpdir/$mode-trace.csv" "$output/"
done
rmdir "$tmpdir"

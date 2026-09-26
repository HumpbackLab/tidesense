#!/usr/bin/env bash
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work=${VIO_COMPARE_WORK:-"$sdk/install/vio-compare-20260924-121320"}
base="$work/ov-schurvins"
fisheye="$sdk/install/fisheye-benchmark/openvins"
target=${1:-all}
case "$target" in uzh|tum|tidesense|all) ;; *) echo 'Usage: run-host.sh [uzh|tum|tidesense|all]' >&2; exit 2 ;; esac

make_config() {
  local src=$1 dst=$2
  python3 - "$src" "$dst" <<'PY'
import re, sys
from pathlib import Path
text = Path(sys.argv[1]).read_text()
for key in ('use_fej', 'calib_cam_extrinsics', 'calib_cam_intrinsics'):
    text, count = re.subn(rf'(?m)^({key}:)\s*(true|false)', rf'\1 false', text)
    if count != 1:
        raise SystemExit(f'Expected exactly one {key} in {sys.argv[1]}')
if not re.search(r'(?m)^feat_rep_msckf:\s*["\']?GLOBAL_3D', text):
    raise SystemExit('ov_SchurVINS requires GLOBAL_3D MSCKF features')
Path(sys.argv[2]).write_text(text)
PY
}

mkdir -p "$base/results"
failures=0
run_one() {
  local name=$1 config=$2 input=$3 binary=$4
  local out="$base/results/$name"
  mkdir -p "$out"
  cp -a "$(dirname -- "$config")/." "$out/"
  make_config "$config" "$out/estimator_config.yaml"
  if "$base/build-host/$binary" "$out/estimator_config.yaml" "$input" \
      "$out/trajectory.csv" 0 > "$out/run.log" 2>&1; then
    echo 0 > "$out/exit.txt"
    tail -1 "$out/run.log"
  else
    status=$?
    echo "$status" > "$out/exit.txt"
    echo "$name failed with exit $status; see $out/run.log" >&2
    failures=1
  fi
}

if [[ "$target" == uzh || "$target" == all ]]; then
  run_one uzh "$fisheye/uzhfpv_indoor/estimator_config.yaml" "$fisheye/uzh-input" run_record
fi
if [[ "$target" == tum || "$target" == all ]]; then
  run_one tum "$fisheye/tum_vi/estimator_config.yaml" "$fisheye/tum-input" run_record
fi
if [[ "$target" == tidesense || "$target" == all ]]; then
  local_data="$sdk/install/dataset-analysis-20260924-174756"
  run_one tidesense "$local_data/openvins-dynamic-config/estimator_config.yaml" "$local_data/replay" run_record_tidesense
fi
exit "$failures"

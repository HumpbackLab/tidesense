#!/usr/bin/env bash
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../.." && pwd)
work=${VIO_COMPARE_WORK:-"$sdk/install/vio-compare-20260924-121320"}
source "$here/sources.lock"

fetch_source() {
  local url=$1 rev=$2 dir=$3
  if [[ ! -d "$dir/.git" ]]; then
    mkdir -p "$(dirname -- "$dir")"
    git clone "$url" "$dir"
    git -C "$dir" checkout --detach "$rev"
  fi
  local actual
  actual=$(git -C "$dir" rev-parse HEAD)
  if [[ "$actual" != "$rev" ]]; then
    echo "Source revision mismatch: $dir is $actual, expected $rev" >&2
    exit 1
  fi
}

apply_once() {
  local dir=$1 patch=$2
  if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
    return
  fi
  git -C "$dir" apply --check "$patch"
  git -C "$dir" apply "$patch"
}

fetch_source "$VINS_MONO_URL" "$VINS_MONO_REV" "$work/vins-mono/upstream"
fetch_source "$VINS_FUSION_URL" "$VINS_FUSION_REV" "$work/vins-fusion/VINS-Fusion"
fetch_source "$OPENVINS_URL" "$OPENVINS_REV" "$work/openvins/open_vins"
apply_once "$work/vins-fusion/VINS-Fusion" "$here/patches/vins-fusion-offline.patch"
apply_once "$work/openvins/open_vins" "$here/patches/openvins-offline.patch"

mkdir -p "$work/vins-mono/standalone" "$work/vins-fusion/shim" "$work/openvins"
cp -a "$here/vins-mono/standalone/." "$work/vins-mono/standalone/"
cp -a "$here/vins-fusion/shim/." "$work/vins-fusion/shim/"
for file in export_tum.py run-board.sh; do
  cp "$here/vins-mono/$file" "$work/vins-mono/$file"
done
for file in CMakeLists.txt offline.cpp generate_config.py run-board.sh run-board-rotated.sh to_tum.py analyze.py; do
  cp "$here/vins-fusion/$file" "$work/vins-fusion/$file"
done
for file in run_record.cpp make_config.py toolchain.cmake run-board.sh; do
  cp "$here/openvins/$file" "$work/openvins/$file"
done
echo "Synchronized maintained sources into $work"

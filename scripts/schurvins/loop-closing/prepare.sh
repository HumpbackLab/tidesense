#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work="$sdk/install/schur-loop-closing"
deps=${SCHUR_LOOP_DEPS:-"$work/deps"}
mkdir -p "$deps" "$work/vocabulary"
fetch() {
  local url=$1 rev=$2 dir=$3
  if [[ ! -d "$dir/.git" ]]; then
    git init "$dir"
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch --depth 1 origin "$rev"
    git -C "$dir" checkout --detach FETCH_HEAD
  fi
  [[ $(git -C "$dir" rev-parse HEAD) == "$rev" ]] || {
    echo "Unexpected revision in $dir" >&2; exit 1;
  }
}
fetch https://github.com/dorian3d/DBoW2.git 3924753db6145f12618e7de09b7e6b258db93c6e "$deps/dbow2"
fetch https://github.com/ethz-asl/opengv.git 7542b890f4d8655119c80763795475bec4667fc9 "$deps/opengv"
voc=voc_VI01_8X4.yml.gz
if [[ ! -f "$work/vocabulary/$voc" ]]; then
  # Stop extraction after this small vocabulary; the upstream archive is 96 MiB.
  # curl may report a closed pipe when tar has obtained the requested member.
  (set +o pipefail
   curl -fL --retry 2 --max-time 300 https://rpg.ifi.uzh.ch/svo2/vocabularies.tar.gz |
     tar -xz --occurrence -C "$work/vocabulary" "$voc")
fi
printf '%s  %s\n' da0a9d0c372e8a1b782839eaacb005b27811cf8405fb243909baebc0a2f2f2e0 "$work/vocabulary/$voc" | sha256sum -c -
if ! git -C "$sdk/SchurVINS" apply --reverse --check "$here/upstream.patch" 2>/dev/null; then
  git -C "$sdk/SchurVINS" apply --check "$here/upstream.patch"
  git -C "$sdk/SchurVINS" apply "$here/upstream.patch"
fi
ceres=${SCHUR_LOOP_CERES:-"$sdk/install/vio-compare-20260924-121320/deps/ceres"}
[[ -f "$ceres/lib/libceres.a" ]] || {
  echo "Set SCHUR_LOOP_CERES to the AArch64 static Ceres prefix (vio-compare dependencies)." >&2; exit 1;
}

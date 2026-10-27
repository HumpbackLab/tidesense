#!/bin/sh
set -eu
base=${1:-/mnt/data/depth-tpu-match}
run() {
 label=$1; shift
 echo "START $label"
 "$base/tpu_sad_probe" "$@" --csv "$base/$label.csv" > "$base/$label.log" 2>&1 || { cat "$base/$label.log"; exit 1; }
 tail -2 "$base/$label.log"
}
for mode in tree direct; do
 for kind in random plane flat extreme occlusion; do
  run "$mode-odd-$kind" --width 97 --height 57 --disparities 7 --runs 2 --case "$kind" --aggregation "$mode"
 done
done
for kind in plane flat extreme occlusion; do
 run "tree-640-$kind" --width 640 --height 400 --disparities 64 --runs 2 --case "$kind"
done
run tree-320-random --width 320 --height 200 --disparities 32 --runs 10
run tree-640-random --width 640 --height 400 --disparities 64 --runs 10
for mode in direct tree; do
 run "final-$mode-320-real" --width 320 --height 200 --disparities 32 --runs 20 --aggregation "$mode" --case real --reference "$base/320-ref.pgm" --target "$base/320-target.pgm"
 run "final-$mode-640-real" --width 640 --height 400 --disparities 64 --runs 20 --aggregation "$mode" --case real --reference "$base/640-ref.pgm" --target "$base/640-target.pgm"
done
for kind in plane extreme; do
 run "final-tree-640-$kind" --width 640 --height 400 --disparities 64 --runs 5 --case "$kind"
done
echo ALL_PASSED

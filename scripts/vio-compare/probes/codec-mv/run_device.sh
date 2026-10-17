#!/bin/sh
# Run on TideSense when no other program owns the multimedia pipeline.
set -eu
cd "${1:-/mnt/data/venc-mv-probe-20261017}"
for name in shift-8-4 shift-neg6-2 quarter split quadrants dynamic; do
  frames=12
  [ "$name" != shift-8-4 ] || frames=16
  dir="csv-$name"
  mkdir -p "$dir"
  VENC_MV_PROBE_DIR="$PWD/$dir" timeout 30 ./sample_venc_probe \
    -c 265 -w 640 -h 400 -i "$name.yuv" -o "$dir/stream" -n "$frames" \
    --rcMode=4 --iqp=20 --pqp=20 --gop=60 --gopMode=0 \
    --srcFramerate=20 --framerate=20 --getstream-timeout=3000 --sendframe-timeout=3000 \
    > "$dir/run.log" 2>&1
  count=$(grep -c '^ENCODED ' "$dir/run.log")
  [ "$count" -eq "$frames" ] || { echo "$name: expected $frames, encoded $count" >&2; exit 1; }
  echo "$name: encoded $count frames"
done

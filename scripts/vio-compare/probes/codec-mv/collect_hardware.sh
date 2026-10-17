#!/bin/sh
# Board-only, multimedia idle. Inputs are raw PGM paths + original ns timestamps.
set -eu
cd /mnt/data/openvins-codec-shallow
trap './cleanup_idle --probe-idle > cleanup-after.log 2>&1' 0
for name in fpv tide; do
  ./cleanup_idle --probe-idle > "$name/cleanup-before.log" 2>&1
  frames=885; height=400; fps=20
  if [ "$name" = fpv ]; then frames=1970; height=480; fps=30; fi
  truncate -s "$((640 * height * 3 / 2 * frames))" "$name/input-sparse.yuv"
  VENC_MV_FRAME_LIST="$name/frames.txt" VENC_MV_CACHE="$name/vectors.bin" \
    timeout 180 ./sample_venc_probe -c 265 -w 640 -h "$height" \
      -i "$name/input-sparse.yuv" -o "$name/hardware" -n "$frames" \
      --rcMode=4 --iqp=20 --pqp=20 --gop=60 --gopMode=0 \
      --srcFramerate="$fps" --framerate="$fps" \
      --getstream-timeout=3000 --sendframe-timeout=3000 > "$name/encode.log" 2>&1
  count=$(grep -c '^ENCODED ' "$name/encode.log")
  [ "$count" -eq "$frames" ] || { tail -n 20 "$name/encode.log"; exit 1; }
  echo "$name: encoded $count frames"
done

#!/bin/sh
# Offline single-thread FP64 replay. Encoder costs are measured separately.
set -eu
cd /mnt/data/openvins-codec-shallow
mkdir -p a53-results
rep=1
while [ "$rep" -le "${1:-1}" ]; do
  cases="${OV_CODEC_SHALLOW_CASES:-base mvfull shallow}"
  if [ "$rep" -eq 2 ] && [ -z "${OV_CODEC_SHALLOW_CASES:-}" ]; then cases="shallow mvfull base"; fi
  for name in ${OV_CODEC_SHALLOW_DATASETS:-fpv tide}; do
    ahead=0.006
    [ "$name" != fpv ] || ahead=0.03
    for case in $cases; do
      mode=rect; lk=full
      [ "$case" != base ] || mode=off
      [ "$case" != shallow ] || lk=shallow
      prefix="a53-results/$name-$case-rep$rep"
      rm -f "$prefix.json"
      OV_CODEC_IMU_AHEAD="$ahead" OV_CODEC_MV_MODE="$mode" OV_CODEC_MV_LK="$lk" \
        OV_CODEC_MV_BIN="$PWD/$name/vectors.bin" \
        OV_CODEC_MV_DETAIL="$PWD/$prefix-detail.csv" OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
        timeout 420 ./run_record "$PWD/a53-config-$name/estimator_config.yaml" \
          "$PWD/$name-record" "$PWD/$prefix-poses.csv" 0 > "$prefix.log" 2>&1
      grep '^DONE ' "$prefix.log"
      cp /tmp/codec-shallow-timing.csv "$prefix-timing.csv"
      printf '{"platform":"a53","codec_cost_in_camera_timer":false}\n' > "$prefix.json"
      echo "$name-$case-rep$rep complete"
    done
  done
  rep=$((rep+1))
done

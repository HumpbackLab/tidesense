#!/bin/sh
# Original FPV images, existing hardware H265 MV cache, FP64 mono150, no ROS.
set -eu
cd /mnt/data/openvins-codec-verify
mkdir -p a53-results
root=/mnt/data/openvins-codec-shallow
for rep in 1 2; do
  cases="base balanced"
  [ "$rep" -ne 2 ] || cases="balanced base"
  for case in $cases; do
    mode=off;lk=full
    if [ "$case" = balanced ]; then mode=rect;lk=verify;fi
    prefix="a53-results/fpv-$case-rep$rep"
    rm -f "$prefix.json"
    OV_CODEC_IMU_AHEAD=.03 OV_CODEC_MV_MODE="$mode" OV_CODEC_MV_LK="$lk" \
      OV_CODEC_MV_VERIFY_MAE=8 OV_CODEC_MV_VERIFY_HAMMING=6 OV_CODEC_MV_VERIFY_REFRESH=5 \
      OV_CODEC_MV_BIN="$root/fpv/vectors.bin" OV_CODEC_MV_DETAIL="$PWD/$prefix-detail.csv" \
      OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
      timeout 420 ./run_record "$root/a53-config-fpv/estimator_config.yaml" \
      "$root/fpv-record" "$PWD/$prefix-poses.csv" 0 > "$prefix.log" 2>&1
    grep '^DONE ' "$prefix.log"
    cp /tmp/codec-shallow-timing.csv "$prefix-timing.csv"
    printf '{"platform":"a53","backend":"FP64","MV_source":"hardware_H265_cache","encoding_included":false}\n' > "$prefix.json"
  done
done

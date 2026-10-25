#!/bin/sh
set -eu
variant=${1:-fp32-packed-householder}
name=${2:-$variant-400}
samples=${3:-400}
export OPENVINS_FP32_PACKED_MA=0 OPENVINS_COMPRESS=givens
precision=fp32
case "$variant" in
  fp64-householder) precision=fp64; OPENVINS_COMPRESS=householder ;;
  fp32) ;;
  fp32-packed) OPENVINS_FP32_PACKED_MA=1 ;;
  fp32-householder) OPENVINS_COMPRESS=householder ;;
  fp32-packed-householder) OPENVINS_FP32_PACKED_MA=1; OPENVINS_COMPRESS=householder ;;
  *) echo "Unknown variant: $variant" >&2; exit 2 ;;
esac
export OPENVINS_RKIVE_DIR=${OPENVINS_BACKEND_DIR:-/root/backend-test-20261025}
mkdir -p "$OPENVINS_RKIVE_DIR/results"
test -x "$OPENVINS_RKIVE_DIR/bin/openvins_mono_$precision"
ln -snf "openvins_mono_$precision" "$OPENVINS_RKIVE_DIR/bin/openvins_mono"
{
    echo "variant=$variant"
    echo "precision=$precision"
    echo "OPENVINS_FP32_PACKED_MA=$OPENVINS_FP32_PACKED_MA"
    echo "OPENVINS_COMPRESS=$OPENVINS_COMPRESS"
    echo "OPENVINS_RUN_MODE=${OPENVINS_RUN_MODE:-baseline}"
} > "$OPENVINS_RKIVE_DIR/results/$name-backend.txt"
sh "$OPENVINS_RKIVE_DIR/run-rkive-board.sh" both "$name" "$samples"

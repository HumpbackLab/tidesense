#!/bin/sh
set -eu
variant=${1:-schur-rkive}
name=${2:-$variant-400}
samples=${3:-400}
export OPENVINS_RKIVE_DIR=${OPENVINS_SCHUR_DIR:-/root/ov-schur-test-20261025}
mode=both
export OPENVINS_FP32_PACKED_MA=0 OPENVINS_COMPRESS=givens
case "$variant" in
  schur-software) binary=ov_schurvins_software; mode=software ;;
  schur-software4) binary=ov_schurvins_rkive; mode=software4 ;;
  schur-rkive) binary=ov_schurvins_rkive ;;
  openvins-fp64-matched) binary=openvins_fp64 ;;
  openvins-optimized-matched)
    binary=openvins_optimized
    OPENVINS_FP32_PACKED_MA=1
    OPENVINS_COMPRESS=householder ;;
  *) echo "Unknown variant: $variant" >&2; exit 2 ;;
esac
test -x "$OPENVINS_RKIVE_DIR/bin/$binary"
mkdir -p "$OPENVINS_RKIVE_DIR/results"
ln -snf "$binary" "$OPENVINS_RKIVE_DIR/bin/openvins_mono"
{
    echo "variant=$variant"
    echo "binary=$binary"
    echo 'FEJ=false, online calibration=false in the shared YAML'
    echo "OPENVINS_FP32_PACKED_MA=$OPENVINS_FP32_PACKED_MA"
    echo "OPENVINS_COMPRESS=$OPENVINS_COMPRESS"
} > "$OPENVINS_RKIVE_DIR/results/$name-variant.txt"
sh "$OPENVINS_RKIVE_DIR/run-rkive-board.sh" "$mode" "$name" "$samples"

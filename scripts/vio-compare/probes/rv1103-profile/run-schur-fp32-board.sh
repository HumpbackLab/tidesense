#!/bin/sh
set -eu
variant=${1:-fp64}
name=${2:-$variant-400}
samples=${3:-400}
export OPENVINS_RKIVE_DIR=${OPENVINS_SCHUR_FP32_DIR:-/root/ov-schur-fp32-test-20261025}
export OPENVINS_SCHUR_PACKED_MA=0
export OPENVINS_SCHUR_COVARIANCE=original
export OPENVINS_SCHUR_SOLVE=native
case "$variant" in
  fp64) precision=fp64 ;;
  fp64-packed) precision=fp64; OPENVINS_SCHUR_PACKED_MA=1 ;;
  fp32) precision=fp32 ;;
  fp32-packed) precision=fp32; OPENVINS_SCHUR_PACKED_MA=1 ;;
  fp64-joseph) precision=fp64; OPENVINS_SCHUR_COVARIANCE=joseph ;;
  fp32-joseph) precision=fp32; OPENVINS_SCHUR_COVARIANCE=joseph ;;
  fp32-packed-joseph) precision=fp32; OPENVINS_SCHUR_PACKED_MA=1; OPENVINS_SCHUR_COVARIANCE=joseph ;;
  fp32-packed-fp64-solve) precision=fp32; OPENVINS_SCHUR_PACKED_MA=1; OPENVINS_SCHUR_COVARIANCE=joseph; OPENVINS_SCHUR_SOLVE=fp64 ;;
  mixed) precision=mixed ;;
  mixed-packed) precision=mixed; OPENVINS_SCHUR_PACKED_MA=1 ;;
  *) echo "Unknown variant: $variant" >&2; exit 2 ;;
esac
test -x "$OPENVINS_RKIVE_DIR/bin/ov_schurvins_$precision"
mkdir -p "$OPENVINS_RKIVE_DIR/results"
ln -snf "ov_schurvins_$precision" "$OPENVINS_RKIVE_DIR/bin/openvins_mono"
unset OPENVINS_SCHUR_NUMERIC_CSV
export OPENVINS_SCHUR_FAILURE_LOG="results/$name-failure.csv"
if [ "$precision" != fp64 ] && [ "${OPENVINS_SCHUR_NUMERIC:-0}" = 1 ]; then
    export OPENVINS_SCHUR_NUMERIC_CSV="results/$name-numeric.csv"
fi
{
    echo "variant=$variant"
    echo "OPENVINS_SCHUR_PACKED_MA=$OPENVINS_SCHUR_PACKED_MA"
    echo "OPENVINS_SCHUR_COVARIANCE=$OPENVINS_SCHUR_COVARIANCE"
    echo "OPENVINS_SCHUR_SOLVE=$OPENVINS_SCHUR_SOLVE"
    echo "OPENVINS_SCHUR_NUMERIC_CSV=${OPENVINS_SCHUR_NUMERIC_CSV:-disabled}"
    echo 'Covariance eigenvalue diagnostics run outside the timed VIO call'
    echo 'FEJ=false, online calibration=false, native initialization'
} > "$OPENVINS_RKIVE_DIR/results/$name-variant.txt"
sh "$OPENVINS_RKIVE_DIR/run-rkive-board.sh" both "$name" "$samples"

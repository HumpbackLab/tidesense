#!/bin/sh
set -eu
variant=${1:-baseline}
name=${2:-$variant-400}
samples=${3:-400}
config_variant=$variant
if [ "$variant" = recommended ]; then
    config_variant=n120-cap8-refine2
    : "${OPENVINS_BATCH_NORMALIZE:=1}" "${OPENVINS_NULLSPACE:=float-givens}"
    : "${OPENVINS_RANSAC_VALID_ONLY:=1}" "${OPENVINS_RANSAC_MAX_ITERS:=100}" "${OPENVINS_GAIN_SOLVE:=direct}"
fi
export OPENVINS_RKIVE_DIR=${OPENVINS_LITE_DIR:-/root/openvins-lite-20261025}
export OPENVINS_CONFIG_DIR="$OPENVINS_RKIVE_DIR/configs/$config_variant"
export OPENVINS_BOARD_RUNNER="$OPENVINS_RKIVE_DIR/run-board.sh"
export OPENVINS_FP32_PACKED_MA=1 OPENVINS_COMPRESS=householder
export OPENVINS_BATCH_NORMALIZE=${OPENVINS_BATCH_NORMALIZE:-1}
export OPENVINS_NULLSPACE=${OPENVINS_NULLSPACE:-givens}
export OPENVINS_RANSAC_VALID_ONLY=${OPENVINS_RANSAC_VALID_ONLY:-0}
export OPENVINS_RANSAC_MAX_ITERS=${OPENVINS_RANSAC_MAX_ITERS:-0}
export OPENVINS_GAIN_SOLVE=${OPENVINS_GAIN_SOLVE:-inverse}
test -f "$OPENVINS_CONFIG_DIR/estimator_config.yaml"
mkdir -p "$OPENVINS_RKIVE_DIR/results"
{
 echo "variant=$variant"
 echo "config_variant=$config_variant"
 echo "OPENVINS_BATCH_NORMALIZE=$OPENVINS_BATCH_NORMALIZE"
 echo "OPENVINS_NULLSPACE=$OPENVINS_NULLSPACE"
 echo "OPENVINS_RANSAC_VALID_ONLY=$OPENVINS_RANSAC_VALID_ONLY"
 echo "OPENVINS_RANSAC_MAX_ITERS=$OPENVINS_RANSAC_MAX_ITERS"
 echo "OPENVINS_GAIN_SOLVE=$OPENVINS_GAIN_SOLVE"
} > "$OPENVINS_RKIVE_DIR/results/$name-variant.txt"
sh "$OPENVINS_RKIVE_DIR/run-rkive-board.sh" both "$name" "$samples"

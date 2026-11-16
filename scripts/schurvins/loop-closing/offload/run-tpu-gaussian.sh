#!/bin/sh
# Experimental approximate Gaussian; ordinary/tuned runners retain CPU blur.
set -eu
app=${SCHUR_LOOP_APP:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}
export SCHUR_ORB_BLUR="${SCHUR_ORB_BLUR:-tpu-q9}"
export SCHUR_ORB_BLUR_VERIFY="${SCHUR_ORB_BLUR_VERIFY:-0}"
exec "$app/run-ive-tuned.sh" "$@"

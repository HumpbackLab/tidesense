#!/bin/sh
# Optional full pixel frontend; run-live.sh itself keeps CPU defaults.
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export OPENVINS_TPU_LK=${OPENVINS_TPU_LK:-1}
export OPENVINS_TPU_FRONTEND=${OPENVINS_TPU_FRONTEND:-1}
export C906_TOOLS_DIR=${C906_TOOLS_DIR:-"$app/../tools"}
exec "$app/with-tpu.sh" "$app/run-live.sh" "$@"

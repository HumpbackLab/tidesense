#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export TIDESENSE_DEV_IMAGE=${TIDESENSE_DEV_IMAGE:-ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04}
exec "$here/scripts/dev-container.sh" shell "$@"

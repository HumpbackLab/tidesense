#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=${TIDESENSE_DEV_IMAGE:-localhost/tidesense-dev:ubuntu24.04}
if [[ ! -d ${XDG_RUNTIME_DIR:-} ]]; then
  export XDG_RUNTIME_DIR="${TMPDIR:-/tmp}/tidesense-podman-$(id -u)"
  mkdir -p -m 700 "$XDG_RUNTIME_DIR"
fi
workspace_args=(--volume "$sdk:$sdk"
  --volume "${TIDESENSE_CALIBRATION_VOLUME:-tidesense-calibration}:/var/lib/containers/storage")
case "${1:-}" in
  build)
    shift
    exec "$sdk/scripts/dev-image/build.sh" -t "$image" "$@"
    ;;
  shell|"")
    [[ $# -eq 0 ]] || shift
    exec podman run --rm -it --privileged --network=host \
      "${workspace_args[@]}" --workdir "$sdk" "$image" bash "$@"
    ;;
  run)
    shift
    exec podman run --rm --privileged --network=host \
      "${workspace_args[@]}" --workdir "$sdk" "$image" "$@"
    ;;
  *)
    echo "Usage: $0 {build|shell|run} [args...]" >&2
    exit 2
    ;;
esac

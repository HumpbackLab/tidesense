#!/usr/bin/env bash
set -eo pipefail

bundled_image=localhost/tidesense-calibration:noetic
if [[ ${KALIBR_IMAGE:-$bundled_image} == "$bundled_image" ]]; then
  expected=$(cat /opt/tidesense/kalibr/kalibr-image-id)
  if ! podman image exists "$expected"; then
    echo 'Preparing bundled Kalibr environment (offline, first use of this image store)…' >&2
    podman load --input /opt/tidesense/kalibr/kalibr.oci.tar
  fi
  # A persistent store may still have an older image under the default tag.
  actual=$(podman image inspect --format '{{.Id}}' "$bundled_image" 2>/dev/null || true)
  if [[ ${actual#sha256:} != "${expected#sha256:}" ]]; then
    podman tag "$expected" "$bundled_image"
  fi
fi

# shellcheck source=/dev/null
source "${TIDESENSE_ROS_SETUP}"
exec "$@"

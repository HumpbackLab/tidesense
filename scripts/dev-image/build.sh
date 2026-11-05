#!/usr/bin/env bash
set -euo pipefail
sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
kalibr_image=${KALIBR_IMAGE:-localhost/tidesense-calibration:noetic}
bundled_image=localhost/tidesense-calibration-bundled:noetic
dev_base=${TIDESENSE_DEV_BASE_IMAGE:-localhost/tidesense-dev-tools:ubuntu24.04}

# The optional base is an already compiled, unmodified Kalibr image. The existing
# builder verifies its pinned source before applying the TideSense IMU policy.
if [[ -n ${TIDESENSE_KALIBR_BASE_IMAGE:-} ]]; then
  "$sdk/tide_configurator/calibration/environment/build.sh" --base "$TIDESENSE_KALIBR_BASE_IMAGE"
else
  "$sdk/tide_configurator/calibration/environment/build.sh"
fi

context=$(mktemp -d "${TMPDIR:-/tmp}/tidesense-dev-image.XXXXXXXX")
trap 'rm -rf -- "$context"' EXIT
mkdir -p "$context/calibration"
cp "$sdk/tide_configurator/requirements.txt" "$context/requirements.txt"
cp "$sdk/tide_configurator/calibration/requirements.txt" "$context/calibration/requirements.txt"
cp "$sdk/scripts/dev-image/entrypoint.sh" "$context/dev-image-entrypoint.sh"

# VFS copies complete parent layers. Flatten Kalibr so a fresh nested store uses
# one root filesystem instead of retaining a copy for every build step.
python3 "$sdk/scripts/dev-image/flatten.py" "$kalibr_image" "$bundled_image" "$context/kalibr-rootfs.tar"
[[ $(podman image inspect --format '{{len .RootFS.Layers}}' "$bundled_image") == 1 ]]
podman image inspect --format '{{.Id}}' "$bundled_image" > "$context/kalibr-image-id"
echo 'Bundling the compiled Kalibr image for offline use…'
podman save --format oci-archive --output "$context/kalibr.oci.tar" "$bundled_image"

if [[ -z ${TIDESENSE_DEV_BASE_IMAGE:-} ]]; then
  podman --cgroup-manager=cgroupfs build --isolation=oci --network=host \
    -f "$sdk/scripts/dev-image/Containerfile.base" -t "$dev_base" "$context"
fi

podman --cgroup-manager=cgroupfs build --isolation=oci --network=host \
  -f "$sdk/scripts/dev-image/Containerfile" --build-arg "DEV_BASE_IMAGE=$dev_base" "$@" "$context"

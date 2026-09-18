#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
revision=1f60227442d25e36365ef5f72cd80b9666d73467
work="$sdk/install/kalibr-podman"
image=${KALIBR_IMAGE:-localhost/tidesense-kalibr:noetic}
mkdir -p "$work/context"
archive="$work/kalibr-$revision.tar.gz"
if ! tar -tzf "$archive" >/dev/null 2>&1; then
  curl -fL --retry 3 --connect-timeout 20 \
    "https://codeload.github.com/ethz-asl/kalibr/tar.gz/$revision" -o "$archive.tmp"
  mv "$archive.tmp" "$archive"
fi
printf '%s  %s\n' 72c02e46817a5da232b3dd3c77550590625c6f8691e8046a36e3f3958947ac61 "$archive" | sha256sum -c -
mkdir -p "$work/context/kalibr-src"
tar -xzf "$archive" --strip-components=1 -C "$work/context/kalibr-src"
cp "$here/Containerfile" "$here/entrypoint.sh" "$here/convert.py" "$work/context/"
# Host networking lets build steps reach an existing localhost HTTP proxy.
podman build --network=host --build-arg "BUILD_JOBS=${KALIBR_BUILD_JOBS:-2}" \
  --build-arg "ROS_IMAGE=${KALIBR_ROS_IMAGE:-public.ecr.aws/docker/library/ros:noetic-ros-base-focal}" \
  --label "org.opencontainers.image.revision=$revision" \
  -t "$image" -f "$work/context/Containerfile" "$work/context"
printf '\nBuilt %s (Kalibr %s)\n' "$image" "$revision"

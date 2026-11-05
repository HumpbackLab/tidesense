#!/usr/bin/env bash
set -euo pipefail
image=${1:-ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04}
empty_seed=$(mktemp)
volume=$(podman volume create)
trap 'podman volume rm --force "$volume" >/dev/null; rm -f -- "$empty_seed"' EXIT

# Use a fresh container/store and disable networking: the image must contain
# everything needed to import Kalibr and start both actual solver entrypoints.
podman run --rm -i --privileged --network=none \
  --volume "$volume:/var/lib/containers/storage" "$image" bash -es <<'CHECK'
python3 -c 'import uvicorn, fastapi, paramiko, numpy, yaml, zstandard, rosbags.rosbag1, rclpy, jinja2, colorama, Crypto; from geometry_msgs.msg import PoseStamped; from std_msgs.msg import Bool'
python3 -m pip check
node --version
test -x /usr/bin/catatonit
command -v xxd
kalibr=localhost/tidesense-calibration:noetic
podman run --rm -i --init --network=none "$kalibr" bash -es <<'KALIBR'
python3 /usr/local/lib/tidesense-calibration-env.py --check
for solver in kalibr_calibrate_cameras kalibr_calibrate_imu_camera; do
  # These pinned Kalibr entrypoints intentionally exit 2 after printing help.
  # Validate its contents as well, so an argparse/import failure cannot pass.
  if help_text=$(xvfb-run -a rosrun kalibr "$solver" --help 2>&1); then
    help_status=0
  else
    help_status=$?
  fi
  printf '%s\n' "$help_text"
  [[ $help_status == 0 || $help_status == 2 ]]
  [[ $help_text == *usage:* && $help_text == *--bag* && $help_text == *--target* ]]
  [[ $help_text != *'Traceback (most recent call last)'* && $help_text != *': error:'* ]]
done
KALIBR
CHECK

# A second container must reuse the stored image even with an unusable archive.
podman run --rm --privileged --network=none \
  --volume "$volume:/var/lib/containers/storage" \
  --volume "$empty_seed:/opt/tidesense/kalibr/kalibr.oci.tar:ro" \
  "$image" bash -ec 'podman image exists localhost/tidesense-calibration:noetic; echo "Cached Kalibr image reused successfully."'

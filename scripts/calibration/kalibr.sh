#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
usage() {
  cat <<'HELP'
Usage: scripts/calibration/kalibr.sh ROS2_BAG_DIR [options]
  --output DIR       New output directory (default: BAG_DIR-kalibr-TIMESTAMP)
  --target YAML      AprilGrid target (default: TideSense A4 6x4, 32 mm)
  --models LEFT RIGHT  Kalibr models (default: pinhole-radtan pinhole-radtan)
  --topics LEFT RIGHT  Raw Image topics (default: /tidesense/{left,right}/image_raw)
  --convert-only     Validate/pair/convert without running Kalibr
Environment: KALIBR_IMAGE (default localhost/tidesense-kalibr:noetic)
Build first: scripts/calibration/kalibr/build.sh
HELP
}
[[ ${1:-} != --help && ${1:-} != -h ]] || { usage; exit 0; }
[[ $# -gt 0 ]] || { usage >&2; exit 2; }
bag=$(realpath -- "$1"); shift
target="$here/tidesense-aprilgrid-a4-6x4-32mm.yaml"
output="${bag}-kalibr-$(date +%Y%m%d-%H%M%S)"
models=(pinhole-radtan pinhole-radtan)
topics=(/tidesense/left/image_raw /tidesense/right/image_raw)
convert_only=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --output|--target) [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      if [[ $1 == --output ]]; then output=$2; else target=$2; fi; shift 2 ;;
    --models|--topics) [[ $# -ge 3 ]] || { usage >&2; exit 2; }
      if [[ $1 == --models ]]; then models=("$2" "$3"); else topics=("$2" "$3"); fi; shift 3 ;;
    --convert-only) convert_only=1; shift ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ -f "$bag/metadata.yaml" ]] || { echo "Not a ROS 2 bag directory: $bag" >&2; exit 2; }
target=$(realpath -- "$target")
[[ -f $target ]] || { echo "Missing target: $target" >&2; exit 2; }
[[ $(id -u) != 0 ]] || { echo "Run this script as your normal user with rootless Podman, not sudo." >&2; exit 2; }
image=${KALIBR_IMAGE:-localhost/tidesense-kalibr:noetic}
podman image exists "$image" || { echo "Build first: $here/kalibr/build.sh" >&2; exit 2; }
output=$(realpath -m -- "$output")
[[ $output != "$bag" && $output != "$bag/"* ]] || { echo 'Output must be outside input bag' >&2; exit 2; }
mkdir -- "$output" # Refuse to overwrite any previous results.
cp -- "$target" "$output/target.yaml"
podman image inspect "$image" > "$output/container-image.json"
printf '%q ' "$0" "$bag" --output "$output" --target "$target" --models "${models[@]}" --topics "${topics[@]}" > "$output/command.txt"
[[ $convert_only == 0 ]] || printf '%s' ' --convert-only' >> "$output/command.txt"
printf '\n' >> "$output/command.txt"
run=(podman run --rm --init --network=none --cap-drop=all --env PYTHONUNBUFFERED=1
     --security-opt=no-new-privileges --volume "$bag:/input:ro"
     --volume "$output:/output:rw" --workdir /output "$image")
"${run[@]}" python3 /usr/local/lib/tidesense-convert.py /input /output/stereo.bag \
  --topics "${topics[@]}" 2>&1 | tee "$output/conversion.log"
if [[ $convert_only == 0 ]]; then
  # Virtual X server also handles Kalibr's GUI imports; no host display/mount needed.
  "${run[@]}" xvfb-run -a rosrun kalibr kalibr_calibrate_cameras \
    --bag /output/stereo.bag --target /output/target.yaml \
    --topics "${topics[@]}" --models "${models[@]}" --dont-show-report \
    2>&1 | tee "$output/kalibr.log"
  for result in stereo-camchain.yaml stereo-results-cam.txt stereo-report-cam.pdf; do
    [[ -s "$output/$result" ]] || { echo "Kalibr did not produce $result; inspect $output/kalibr.log" >&2; exit 1; }
  done
fi
printf '\nResults: %s\n' "$output"

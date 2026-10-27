#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
usage() {
  cat <<'HELP'
Usage: scripts/calibration/kalibr.sh ROS1_FILE.bag|ROS2_BAG_DIR [options]
       scripts/calibration/kalibr.sh --export-schurvins RESULTS_DIR [--output NEW_YAML]
  --output DIR       New output directory (default: BAG_DIR-kalibr-TIMESTAMP)
  --target YAML      AprilGrid target (default: TideSense A4 6x4, 32 mm)
  --lens MODE        wide (pinhole-radtan, default) or fisheye (pinhole-equi)
  --topics LEFT RIGHT  Raw Image topics (default: /tidesense/{left,right}/image_raw)
  --imu-cam          Calibrate camera-IMU and automatically export schurvins.yaml
  --cam YAML         Existing stereo camchain (required with --imu-cam)
  --imu YAML         Measured IMU noise/rate/topic configuration (required with --imu-cam)
                     IMU model is always scale-misalignment on raw SI input
  --estimate-g-sensitivity  Also fit gyro acceleration sensitivity (default: fixed zero)
  --allan            Analyze a stationary ROS 2 IMU bag; write curves and noise YAML
  --imu-topic TOPIC  Allan IMU topic (default: /tidesense/imu)
  --skip-seconds N   Allan: skip N seconds from first header stamp (default: 0)
  --duration N       Allan: analyze at most N seconds after the skip
  --convert-only     Validate/pair/convert without running Kalibr or exporting SchurVINS
  --export-schurvins  Convert existing IMU-camera results locally; no bag/ROS/Podman needed
                     Requires Python 3 with numpy and PyYAML; refuses to overwrite output
Environment: KALIBR_IMAGE (default localhost/tidesense-kalibr:noetic)
Build first: scripts/calibration/kalibr/build.sh
HELP
}
[[ ${1:-} != --help && ${1:-} != -h ]] || { usage; exit 0; }
[[ $# -gt 0 ]] || { usage >&2; exit 2; }
if [[ $1 == --export-schurvins ]]; then
  shift
  exec python3 "$here/kalibr/export_schurvins.py" "$@"
fi
bag=$(realpath -- "$1"); shift
original_args=("$bag" "$@")
target="$here/tidesense-aprilgrid-a4-6x4-32mm.yaml"
output="${bag}-kalibr-$(date +%Y%m%d-%H%M%S)"
models=(pinhole-radtan pinhole-radtan)
topics=(/tidesense/left/image_raw /tidesense/right/image_raw)
topics_set=0
convert_only=0
imu_cam=0
cam=
imu=
estimate_g_sensitivity=0
stereo_options=0
allan=0
allan_options=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --output|--target) [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      if [[ $1 == --output ]]; then output=$2; else target=$2; fi; shift 2 ;;
    --cam|--imu) [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      if [[ $1 == --cam ]]; then cam=$2; else imu=$2; fi; shift 2 ;;
    --imu-cam) imu_cam=1; shift ;;
    --lens) [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      stereo_options=1
      case "$2" in
        wide) models=(pinhole-radtan pinhole-radtan) ;;
        fisheye) models=(pinhole-equi pinhole-equi) ;;
        *) echo 'Supported --lens modes: wide, fisheye' >&2; exit 2 ;;
      esac
      shift 2 ;;
    --estimate-g-sensitivity) estimate_g_sensitivity=1; shift ;;
    --allan) allan=1; shift ;;
    --imu-topic|--skip-seconds|--duration) [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      allan_options+=("$1" "$2"); shift 2 ;;
    --topics) [[ $# -ge 3 ]] || { usage >&2; exit 2; }
      stereo_options=1
      topics=("$2" "$3"); topics_set=1; shift 3 ;;
    --convert-only) convert_only=1; shift ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
if [[ $imu_cam == 0 && $estimate_g_sensitivity == 1 ]]; then
  echo 'IMU model options require --imu-cam' >&2; exit 2
fi
if [[ $allan == 1 ]]; then
  [[ $imu_cam == 0 && -z $cam && -z $imu && $stereo_options == 0 && $convert_only == 0 ]] || {
    echo '--allan cannot be combined with camera calibration or --convert-only options' >&2; exit 2;
  }
elif [[ ${#allan_options[@]} != 0 ]]; then
  echo '--imu-topic, --skip-seconds and --duration require --allan' >&2; exit 2
fi
if [[ $imu_cam == 1 ]]; then
  [[ -n $cam && -n $imu ]] || { echo '--imu-cam requires --cam and --imu YAML files' >&2; exit 2; }
  [[ $stereo_options == 0 ]] || { echo 'IMU-camera mode reads topics/models from --cam; omit --topics and --lens' >&2; exit 2; }
  cam=$(realpath -- "$cam"); imu=$(realpath -- "$imu")
  [[ -f $cam && -f $imu ]] || { echo 'Missing camera/IMU YAML file' >&2; exit 2; }
else
  [[ -z $cam && -z $imu ]] || { echo '--cam and --imu require --imu-cam' >&2; exit 2; }
fi
input_path=/input
if [[ -f "$bag" && "$bag" == *.bag ]]; then
  input_path=/input.bag
  if [[ $topics_set == 0 ]]; then topics=(/cam0/image_raw /cam1/image_raw); fi
else
  [[ -f "$bag/metadata.yaml" ]] || { echo "Expected a ROS 1 .bag file or ROS 2 bag directory: $bag" >&2; exit 2; }
fi
if [[ $allan == 0 ]]; then
  target=$(realpath -- "$target")
  [[ -f $target ]] || { echo "Missing target: $target" >&2; exit 2; }
fi
[[ $(id -u) != 0 ]] || { echo "Run this script as your normal user with rootless Podman, not sudo." >&2; exit 2; }
image=${KALIBR_IMAGE:-localhost/tidesense-kalibr:noetic}
podman image exists "$image" || { echo "Build first: $here/kalibr/build.sh" >&2; exit 2; }
if [[ $imu_cam == 1 ]]; then
  podman run --rm --init --network=none --cap-drop=all --security-opt=no-new-privileges \
    "$image" python3 -c 'import scipy.optimize' || {
      echo "IMU-camera runtime dependency check failed; run: $here/kalibr/build.sh --runtime-only" >&2
      exit 2
    }
fi
output=$(realpath -m -- "$output")
[[ $output != "$bag" && $output != "$bag/"* ]] || { echo 'Output must be outside input bag' >&2; exit 2; }
mkdir -- "$output" # Refuse to overwrite any previous results.
if [[ $allan == 0 ]]; then
  cp -- "$target" "$output/target.yaml"
  cp -- "$here/kalibr/convert.py" "$output/convert.py"
fi
podman image inspect "$image" > "$output/container-image.json"
printf '%q ' "$0" "${original_args[@]}" > "$output/command.txt"
printf '\n' >> "$output/command.txt"
run=(podman run --rm --init --network=none --cap-drop=all --env PYTHONUNBUFFERED=1
     --security-opt=no-new-privileges --volume "$bag:$input_path:ro"
     --volume "$output:/output:rw" --workdir /output "$image")
if [[ $allan == 1 ]]; then
  cp -- "$here/kalibr/allan.py" "$output/allan.py"
  "${run[@]}" python3 /output/allan.py "$input_path" /output "${allan_options[@]}" \
    2>&1 | tee "$output/allan.log"
  printf '\nAllan results (check warnings and null parameters): %s\n' "$output"
  exit 0
fi
bagname=stereo
conversion=(--topics "${topics[@]}")
if [[ $imu_cam == 1 ]]; then
  bagname=imucam
  cp -- "$cam" "$output/camchain-input.yaml"
  cp -- "$imu" "$output/imu-input.yaml"
  conversion=(--cam /output/camchain-input.yaml --imu /output/imu-input.yaml)
fi
"${run[@]}" python3 /output/convert.py "$input_path" "/output/$bagname.bag" \
  "${conversion[@]}" 2>&1 | tee "$output/conversion.log"
if [[ $convert_only == 0 ]]; then
  # Virtual X server also handles Kalibr's GUI imports; no host display/mount needed.
  if [[ $imu_cam == 1 ]]; then
    intrinsic_options=()
    [[ $estimate_g_sensitivity == 1 ]] || intrinsic_options+=(--disable-g-sensitivity)
    python3 "$here/kalibr/calibrate_intrinsics.py" --source "$output" --output "$output" \
      --in-place --image "$image" "${intrinsic_options[@]}"
    results=(imucam-camchain-imucam.yaml imucam-imu.yaml imucam-results-imucam.txt imucam-report-imucam.pdf)
  else
    "${run[@]}" xvfb-run -a rosrun kalibr kalibr_calibrate_cameras \
      --bag /output/stereo.bag --target /output/target.yaml \
      --topics "${topics[@]}" --models "${models[@]}" --dont-show-report \
      2>&1 | tee "$output/kalibr.log"
    results=(stereo-camchain.yaml stereo-results-cam.txt stereo-report-cam.pdf)
  fi
  for result in "${results[@]}"; do
    [[ -s "$output/$result" ]] || { echo "Kalibr did not produce $result; inspect $output/kalibr.log" >&2; exit 1; }
  done
  if [[ $imu_cam == 1 ]]; then
    cp -- "$here/kalibr/export_schurvins.py" "$output/export_schurvins.py"
    "${run[@]}" python3 /output/export_schurvins.py /output \
      2>&1 | tee "$output/schurvins-export.log"
  fi
fi
printf '\nResults: %s\n' "$output"

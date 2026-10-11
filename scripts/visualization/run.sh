#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$here/../calibration/ros2-env.sh"
for argument in "$@"; do
  if [[ $argument == --help || $argument == -h ]]; then
    exec /usr/bin/python3 "$here/vio_path.py" "$@"
  fi
done
/usr/bin/python3 "$here/vio_path.py" --check "$@"
/usr/bin/python3 "$here/vio_path.py" "$@" &
bridge_pid=$!
cleanup() {
  kill -TERM "$bridge_pid" 2>/dev/null || true
  wait "$bridge_pid" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
ros2 run rviz2 rviz2 -d "$here/tidesense-vio.rviz"

#!/usr/bin/env bash
# Optional host companion tool; never installed or started on TideSense.
set -eo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work="$sdk/install/micro-ros"
source "$sdk/scripts/ros2-setup.sh"
tidesense_source_ros2
[[ -f "$work/venv/bin/activate" ]] || { echo 'Run micro_ros/build.sh first' >&2; exit 1; }
source "$work/venv/bin/activate"
set -u
revision=c93ee764e0d2ef4907aeb29233c68cb5f4b56976
if [[ ! -d "$work/agent-src/.git" ]]; then
  git init "$work/agent-src"
  git -C "$work/agent-src" remote add origin https://github.com/micro-ROS/micro-ROS-Agent.git
  git -C "$work/agent-src" fetch --depth 1 origin "$revision"
  git -C "$work/agent-src" checkout --detach FETCH_HEAD
fi
[[ $(git -C "$work/agent-src" rev-parse HEAD) == "$revision" ]] || { echo 'Unexpected Agent revision' >&2; exit 1; }
git -C "$work/agent-src" diff --exit-code HEAD
unset RMW_IMPLEMENTATION
export CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-4}
cd "$work"
colcon --log-base agent-log build --base-paths agent-src firmware/mcu_ws/uros/micro_ros_msgs \
  --build-base agent-build --install-base agent-install --merge-install \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DPYTHON_EXECUTABLE="$work/venv/bin/python3" -DPython3_EXECUTABLE="$work/venv/bin/python3"
echo "Agent built. Source $work/agent-install/local_setup.bash, then:"
echo 'ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888'

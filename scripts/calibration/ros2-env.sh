#!/usr/bin/env bash
# Source in each host ROS 2 preview/record terminal; does not start any processes.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo 'Use: source scripts/calibration/ros2-env.sh' >&2
  exit 1
fi
_tidesense_sdk=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$_tidesense_sdk/scripts/ros2-setup.sh"
tidesense_source_ros2 || return 1
source "$_tidesense_sdk/install/micro-ros/agent-install/local_setup.bash"
# Existing SDK test dependencies can also supply Cyclone when not installed globally.
_tidesense_cyclone="$_tidesense_sdk/install/micro-ros/host-cyclone/opt/ros/$ROS_DISTRO"
if [[ -f "$_tidesense_cyclone/lib/librmw_cyclonedds_cpp.so" ]]; then
  export AMENT_PREFIX_PATH="$_tidesense_cyclone${AMENT_PREFIX_PATH:+:$AMENT_PREFIX_PATH}"
  export LD_LIBRARY_PATH="$_tidesense_cyclone/lib:$_tidesense_cyclone/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
export ROS_DOMAIN_ID=0
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI="file://$_tidesense_sdk/scripts/schurvins/micro_ros/calibration-cyclone.xml"
unset _tidesense_cyclone _tidesense_sdk

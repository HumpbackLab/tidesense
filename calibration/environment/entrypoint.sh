#!/usr/bin/env bash
set -eo pipefail
setup_ros() {
  source /opt/ros/noetic/setup.bash
  source /catkin_ws/devel/setup.bash
}
setup_ros
export ROS_HOME=/tmp/tidesense-calibration/.ros
export MPLCONFIGDIR=/tmp/tidesense-calibration/matplotlib
mkdir -p "$ROS_HOME" "$MPLCONFIGDIR"
exec "$@"

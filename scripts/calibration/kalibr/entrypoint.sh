#!/usr/bin/env bash
set -eo pipefail
# Do not let setup.bash interpret Kalibr's command-line options (e.g. --help).
setup_ros() {
  source /opt/ros/noetic/setup.bash
  source /catkin_ws/devel/setup.bash
}
setup_ros
export HOME=/tmp/tidesense-home ROS_HOME=/tmp/tidesense-home/.ros
export MPLCONFIGDIR=/tmp/tidesense-home/matplotlib
mkdir -p "$ROS_HOME" "$MPLCONFIGDIR"
exec "$@"

#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/noetic/setup.bash
source /catkin_ws/devel/setup.bash
export HOME=/tmp/tidesense-home ROS_HOME=/tmp/tidesense-home/.ros
export MPLCONFIGDIR=/tmp/tidesense-home/matplotlib
mkdir -p "$ROS_HOME" "$MPLCONFIGDIR"
exec "$@"

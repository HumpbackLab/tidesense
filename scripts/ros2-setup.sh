#!/usr/bin/env bash
# Source the ROS 2 installation selected by the caller or installed on this host.
tidesense_source_ros2() {
  local setup=${TIDESENSE_ROS_SETUP:-${MICRO_ROS_HOST_SETUP:-}}
  local candidates=()
  if [[ -z "$setup" && -n ${ROS_DISTRO:-} ]]; then
    setup="/opt/ros/$ROS_DISTRO/setup.bash"
  fi
  if [[ -z "$setup" ]]; then
    shopt -s nullglob
    candidates=(/opt/ros/*/setup.bash)
    shopt -u nullglob
    if (( ${#candidates[@]} != 1 )); then
      echo 'Set TIDESENSE_ROS_SETUP to a ROS 2 setup.bash (or source ROS 2 first).' >&2
      return 1
    fi
    setup=${candidates[0]}
  fi
  if [[ ! -f "$setup" ]]; then
    echo "ROS 2 setup file is missing: $setup" >&2
    return 1
  fi
  source "$setup"
  if [[ ${ROS_VERSION:-} != 2 || -z ${ROS_DISTRO:-} ]]; then
    echo "Expected a ROS 2 environment from $setup" >&2
    return 1
  fi
}

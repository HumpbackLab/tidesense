# Source this file; private host DDS libraries already built by SDK micro-ROS tests.
_tide_dds_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
source /opt/ros/humble/setup.bash
source "$_tide_dds_root/install/micro-ros/agent-install/local_setup.bash"
_tide_host_dds="$_tide_dds_root/install/micro-ros/host-cyclone/opt/ros/humble"
export AMENT_PREFIX_PATH="$_tide_host_dds:$AMENT_PREFIX_PATH"
export LD_LIBRARY_PATH="$_tide_host_dds/lib:$_tide_host_dds/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
unset _tide_dds_root _tide_host_dds

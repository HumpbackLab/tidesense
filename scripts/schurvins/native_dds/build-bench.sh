#!/usr/bin/env bash
set -eo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work="$sdk/install/native-dds"
source /opt/ros/humble/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export AMENT_PREFIX_PATH="$work/ws/install:$AMENT_PREFIX_PATH"
export CMAKE_PREFIX_PATH="$work/ws/install:${CMAKE_PREFIX_PATH:-}"
cmake -S "$here" -B "$work/bench-build" \
 -DCMAKE_TOOLCHAIN_FILE="$here/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DRMW_IMPLEMENTATION=rmw_cyclonedds_cpp -DRCL_LOGGING_IMPLEMENTATION=rcl_logging_noop
cmake --build "$work/bench-build" -j4
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
mkdir -p "$work/package/lib"
python3 - "$sdk" "$here" "${cross}g++" <<'PY'
import pathlib,subprocess,sys,shutil
sdk=pathlib.Path(sys.argv[1]);here=pathlib.Path(sys.argv[2]);out=sdk/'install/native-dds/package'
headers=sdk/'install/micro-ros/firmware/build/include'
args=[sys.argv[3],'-std=c++14','-O3','-mcpu=cortex-a53','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DSCHUR_WITH_MICRO_ROS=1','-I'+str(headers)]
args+=['-I'+str(p) for p in headers.iterdir() if p.is_dir()]
args+=[str(here/'bench.cpp'),str(here.parent/'micro_ros_bridge.cpp'),str(sdk/'install/micro-ros/firmware/build/libmicroros.a'),'-lpthread','-lrt','-lm','-o',str(out/'micro_bench')]
subprocess.run(args,check=True)
shutil.copy2(sdk/'install/native-dds/bench-build/native_bench',out/'native_bench')

PY
python3 "$here/package-runtime.py" "$work/package/lib" --binary "$work/package/native_bench"
"${cross}strip" --strip-unneeded "$work/package/native_bench" "$work/package/micro_bench" "$work/package/lib/"*.so*
"${cross}readelf" -dW "$work/package/native_bench" > "$work/native-needed.txt"
echo "$work/package"

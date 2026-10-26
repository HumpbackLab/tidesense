#!/usr/bin/env bash
# Build the pinned A53 native ROS 2 / Cyclone DDS runtime.
set -eo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work="$sdk/install/native-dds"
source /opt/ros/humble/setup.bash
if [[ ! -x "$work/venv/bin/colcon" ]]; then
  python3 -m venv --system-site-packages "$work/venv"
  "$work/venv/bin/pip" install colcon-common-extensions vcstool 'empy==3.3.4' lark catkin_pkg
fi
source "$work/venv/bin/activate"
set -u
mkdir -p "$work/ws/src"
python3 - "$sdk" "$here" <<'PY'
import io,pathlib,subprocess,sys,tarfile,json,yaml
sdk=pathlib.Path(sys.argv[1]);here=pathlib.Path(sys.argv[2]);root=sdk/'install/native-dds/ws/src'
cached=sdk/'install/micro-ros/firmware/mcu_ws'
manifest={}
for name,spec in yaml.safe_load((here/'sources.repos').read_text())['repositories'].items():
 dest=root/name.replace('/','_')
 # Keep the original standalone dependency directory names used by the experiment.
 if '/' not in name: dest=root/name
 rev=spec['version'];manifest[name]=rev
 if dest.exists():
  if (dest/'.git').is_dir():
   actual=subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
   if actual!=rev:raise SystemExit('Unexpected native DDS revision: '+str(dest))
  continue
 src=cached/name
 available=(src/'.git').is_dir() and subprocess.run(['git','-C',str(src),'cat-file','-e',rev+'^{commit}'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
 if not available:
  src=sdk/'install/native-dds/source-cache'/name
  src.parent.mkdir(parents=True,exist_ok=True)
  if not (src/'.git').is_dir():
   subprocess.run(['git','init',str(src)],check=True)
   subprocess.run(['git','-C',str(src),'remote','add','origin',spec['url']],check=True)
  for attempt in range(3):
   fetched=subprocess.run(['git','-C',str(src),'fetch','--depth','1','origin',rev])
   if fetched.returncode==0:break
  fetched.check_returncode()
  subprocess.run(['git','-C',str(src),'checkout','--detach',rev],check=True)
 data=subprocess.check_output(['git','-C',str(src),'archive',rev])
 dest.mkdir();tarfile.open(fileobj=io.BytesIO(data)).extractall(dest)
# Use the host IDL compiler and disable unneeded runtime features.
for name in ['ros2_rcl_logging/rcl_logging_spdlog','uros_rclc/rclc_examples']:(root/name/'COLCON_IGNORE').touch()
(sdk/'install/native-dds/sources.json').write_text(json.dumps(manifest,indent=2))
PY
export CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-6}
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
cd "$work/ws"
colcon build --merge-install --executor sequential --packages-up-to rclc geometry_msgs std_msgs rmw_cyclonedds_cpp \
 --cmake-args -Ulibyaml_vendor_DIR -Uyaml_DIR -DCMAKE_TOOLCHAIN_FILE="$here/toolchain.cmake" \
 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF \
 -DPYTHON_EXECUTABLE="$work/venv/bin/python3" \
 -DPython3_EXECUTABLE="$work/venv/bin/python3" \
 -DRMW_IMPLEMENTATION=rmw_cyclonedds_cpp -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON \
 -DRCL_LOGGING_IMPLEMENTATION=rcl_logging_noop -DTRACETOOLS_DISABLED=ON -DTRACETOOLS_STATUS_CHECKING_TOOL=OFF \
 -DBUILD_IDLC=OFF -DBUILD_DDSPERF=OFF -DBUILD_EXAMPLES=OFF -DENABLE_SECURITY=OFF -DENABLE_SSL=OFF -DENABLE_SHM=OFF \
 -DCMAKE_POSITION_INDEPENDENT_CODE=ON

# Carry all dependency license notices into deployed applications.
python3 - "$work" <<'PY_LICENSES'
from pathlib import Path
import shutil,sys
root=Path(sys.argv[1]);dest=root/'licenses'
for path in (root/'ws/src').rglob('*'):
 if path.is_file() and '.git' not in path.parts and path.name.lower().startswith(('license','copying','notice')):
  target=dest/path.relative_to(root/'ws/src');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
PY_LICENSES

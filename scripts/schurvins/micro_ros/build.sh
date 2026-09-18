#!/usr/bin/env bash
# Build the pinned Humble rcl/rclc/RMW/XRCE stack as an A53 static archive.
set -eo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../.." && pwd)
work="$sdk/install/micro-ros"
ros_setup=${MICRO_ROS_HOST_SETUP:-/opt/ros/humble/setup.bash}
[[ -f "$ros_setup" ]] || { echo "Install host ROS 2 Humble development tools or set MICRO_ROS_HOST_SETUP" >&2; exit 1; }
source "$ros_setup"
[[ "$ROS_DISTRO" == humble ]] || { echo "This manifest targets ROS 2 Humble" >&2; exit 1; }
set -u
mkdir -p "$work/firmware/mcu_ws"
if [[ ! -x "$work/venv/bin/colcon" ]]; then
  python3 -m venv --system-site-packages "$work/venv"
  "$work/venv/bin/pip" install colcon-common-extensions vcstool 'empy==3.3.4' lark catkin_pkg
fi
source "$work/venv/bin/activate"
# Fetch exact commits. Refuse to overwrite another revision or modified source.
python3 - "$here/sources.repos" "$work/firmware/mcu_ws" "$here/0001-isolate-client-key-rng.patch" <<'PY'
import pathlib, subprocess, sys, yaml
root = pathlib.Path(sys.argv[2])
for name, spec in yaml.safe_load(pathlib.Path(sys.argv[1]).read_text())['repositories'].items():
    target = root / name
    if not (target / '.git').is_dir():
        target.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'init', str(target)], check=True)
        subprocess.run(['git', '-C', str(target), 'remote', 'add', 'origin', spec['url']], check=True)
    # An interrupted initial download may leave an initialized repo without HEAD.
    head = subprocess.run(['git', '-C', str(target), 'rev-parse', '--verify', 'HEAD'],
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    if head.returncode != 0:
        for attempt in range(3):
            p = subprocess.run(['git', '-C', str(target), 'fetch', '--depth', '1', 'origin', spec['version']])
            if p.returncode == 0:
                break
        p.check_returncode()
        subprocess.run(['git', '-C', str(target), 'checkout', '--detach', 'FETCH_HEAD'], check=True)
    rev = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
    if rev != spec['version']:
        raise SystemExit(f'Unexpected revision: {target}; source left untouched')
    if name == 'uros/rmw_microxrcedds':
        patch = pathlib.Path(sys.argv[3])
        diff = subprocess.check_output(['git', '-C', str(target), 'diff', 'HEAD'])
        if diff and diff != patch.read_bytes():
            raise SystemExit(f'Unexpected local changes: {target}; source left untouched')
        if not diff:
            subprocess.run(['git', '-C', str(target), 'apply', '--check', str(patch)], check=True)
            subprocess.run(['git', '-C', str(target), 'apply', str(patch)], check=True)
    else:
        subprocess.run(['git', '-C', str(target), 'diff', '--exit-code', 'HEAD'], check=True)
for name in ['ros2/rcl', 'ros2/rcpputils', 'ros2/rcl_logging/rcl_logging_spdlog',
             'ros2/rosidl/rosidl_typesupport_introspection_cpp',
             'uros/rcl/rcl_yaml_param_parser', 'uros/rclc/rclc_examples']:
    (root / name / 'COLCON_IGNORE').touch()
PY
export CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-4}
export RMW_IMPLEMENTATION=rmw_microxrcedds
(
  cd "$work/firmware/mcu_ws"
  colcon build --merge-install --executor sequential \
    --packages-ignore-regex '.*_cpp' --packages-up-to rclc geometry_msgs std_msgs \
    --metas "$here/colcon.meta" --cmake-args \
    -DCMAKE_TOOLCHAIN_FILE="$here/toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
    -DRMW_IMPLEMENTATION=rmw_microxrcedds \
    -DTHIRDPARTY=ON -DPYTHON_EXECUTABLE="$work/venv/bin/python3" \
    -DPython3_EXECUTABLE="$work/venv/bin/python3"
)
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
mkdir -p "$work/firmware/build"
python3 - "$work/firmware" "${cross}ar" <<'PY'
import pathlib, subprocess, sys, shutil
root = pathlib.Path(sys.argv[1]); libraries = sorted((root/'mcu_ws/install/lib').rglob('*.a'))
if not libraries:
    raise SystemExit('No micro-ROS static libraries produced')
output = root/'build/libmicroros.a'
script = 'CREATE ' + str(output) + '\n'
script += ''.join('ADDLIB ' + str(p) + '\n' for p in libraries)
script += 'SAVE\nEND\n'
subprocess.run([sys.argv[2], '-M'], input=script, text=True, check=True)
headers = root/'build/include'
if headers.exists(): shutil.rmtree(headers)
shutil.copytree(root/'mcu_ws/install/include', headers)
PY
cp "$here/sources.repos" "$here/colcon.meta" "$here/0001-isolate-client-key-rng.patch" "$work/firmware/build/"
# Ship upstream license notices with the application package.
python3 - "$here/sources.repos" "$work/firmware" <<'PY'
import pathlib, sys, yaml, shutil
root = pathlib.Path(sys.argv[2]); dest = root/'build/licenses'; dest.mkdir(exist_ok=True)
for name in yaml.safe_load(pathlib.Path(sys.argv[1]).read_text())['repositories']:
    repo = root/'mcu_ws'/name
    for p in repo.rglob('*'):
        if p.is_file() and (p.name.lower().startswith(('license', 'copying', 'notice')) or p.name == '3rd-party-licenses.txt') and '.git' not in p.parts:
            target = dest/name/p.relative_to(repo)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, target)
PY
printf 'Built micro-ROS: %s/firmware/build/libmicroros.a\n' "$work"

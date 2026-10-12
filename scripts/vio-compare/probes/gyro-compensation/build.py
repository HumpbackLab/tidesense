#!/usr/bin/env python3
"""Build an isolated FP64 replay probe from the verified host diagnostic build."""
import argparse
import concurrent.futures
import difflib
from pathlib import Path
import re
import shlex
import shutil
import subprocess

repo = Path(__file__).resolve().parents[4]
here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--base', type=Path, default=repo / 'install/replay-20261011-213326/host-diagnosis')
args = parser.parse_args()
work = args.work.resolve()
base = args.base.resolve()
work.mkdir(parents=True, exist_ok=False)
src = work / 'source'
shutil.copytree(base / 'base_source', src, ignore=shutil.ignore_patterns('.git', '.codegraph'))
for name in ['ovdiag.h', 'numeric_probe.h']:
    shutil.copyfile(base / name, work / name)
shutil.copyfile(here / 'gyro_frontend.hpp', work / 'gyro_frontend.hpp')

track = src / 'ov_core/src/track/TrackKLT.cpp'
before = track.read_text()
after = before.replace('#include "TrackKLT.h"', '#include "TrackKLT.h"\n#include "gyro_frontend.hpp"', 1)
needle = '  cv::calcOpticalFlowPyrLK(img0pyr, img1pyr, pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit, cv::OPTFLOW_USE_INITIAL_FLOW);'
assert after.count(needle) == 1
after = after.replace(needle, '''#if OPENVINS_GYRO_FRONTEND
  if (id0 != id1 || !gyro_frontend::match(id0, camera_calib.at(id0), img0pyr, img1pyr,
                                         pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit))
#endif
''' + needle)
track.write_text(after)
(work / 'openvins.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                     fromfile='a/ov_core/src/track/TrackKLT.cpp', tofile='b/ov_core/src/track/TrackKLT.cpp')))

runner = work / 'run_record.cpp'
before = (base / 'run_record.cpp').read_text()
after = before.replace('#include <chrono>', '#include <chrono>\n#include <algorithm>\n#include "state/Propagator.h"\n#include "gyro_frontend.hpp"', 1)
after = after.replace('    double total_ms = 0;', '    double prev_camera_time = -1.;\n    double total_ms = 0;', 1)
needle = '      const auto start = std::chrono::steady_clock::now();\n      system.feed_measurement_camera(image);'
assert after.count(needle) == 1
after = after.replace(needle, '''      const auto start = std::chrono::steady_clock::now();
#if OPENVINS_GYRO_FRONTEND
      if (ov_core::gyro_frontend::context().mode && prev_camera_time > 0.) {
        auto state = system.get_state();
        const double offset = state->_calib_dt_CAMtoIMU->value()(0);
        const double from = prev_camera_time + offset, to = camera_time + offset;
        auto first = std::lower_bound(imu.begin(), imu.end(), from,
                      [](const ov_core::ImuData &m, double t) { return m.timestamp < t; });
        if (first != imu.begin()) --first;
        auto last = std::upper_bound(first, imu.end(), to,
                      [](double t, const ov_core::ImuData &m) { return t < m.timestamp; });
        if (last != imu.end()) ++last;
        std::vector<ov_core::ImuData> window(first, last);
        auto readings = ov_msckf::Propagator::select_imu_readings(window, from, to, false);
        std::map<size_t, Eigen::Matrix3d> rotations;
        if (readings.size() >= 2) {
          Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
          const Eigen::Vector3d bias = state->_imu->bias_g();
          for (size_t i = 1; i < readings.size(); ++i) {
            double dt = readings[i].timestamp - readings[i-1].timestamp;
            Eigen::Vector3d w = .5 * (readings[i].wm + readings[i-1].wm) - bias;
            const double angle = w.norm() * dt;
            if (dt > 0 && angle > 0) R = Eigen::AngleAxisd(-angle, w.normalized()).toRotationMatrix() * R;
          }
          for (size_t cam = 0; cam < 2; ++cam) {
            Eigen::Matrix3d R_ic = state->_calib_IMUtoCAM.at(cam)->Rot();
            rotations[cam] = R_ic * R * R_ic.transpose();
          }
        }
        ov_core::gyro_frontend::set_rotations(rotations, camera_time);
      }
#endif
      system.feed_measurement_camera(image);
      prev_camera_time = camera_time;''')
runner.write_text(after)
(work / 'run_record.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                        fromfile='a/run_record.cpp', tofile='b/run_record.cpp')))

host = repo / 'install/openvins-backend-f32/host-fp64'
flags_text = (host / 'build/CMakeFiles/ov_msckf_lib.dir/flags.make').read_text()
flags = []
for key in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']:
    value = re.search(r'^' + key + r' = (.*)$', flags_text, re.M).group(1)
    flags += shlex.split(value.replace(str(host / 'base_source'), str(src)))
flags += ['-I' + str(work), '-DOPENVINS_GYRO_FRONTEND=1',
          '-DOPENVINS_LK_WINDOW_16X15=1', '-DOPENVINS_LK_FILTER_VALID=1']

def compile_file(file):
    obj = work / (file.name + '.o')
    subprocess.run(['g++-11', *flags, '-c', str(file), '-o', str(obj)], check=True)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    objects = list(pool.map(compile_file, [track, runner]))
lib = work / 'libov_msckf_gyro.a'
shutil.copyfile(base / 'libov_msckf_diag.a', lib)
subprocess.run(['ar', 'r', str(lib), str(objects[0])], check=True)
link = shlex.split((host / 'build/CMakeFiles/run_record.dir/link.txt').read_text())
output = work / 'run_record_gyro'
command = []
i = 0
while i < len(link):
    word = link[i]
    if word.endswith('run_record.cpp.o'): command.append(str(objects[1]))
    elif word == 'libov_msckf_lib.a': command.append(str(lib))
    elif word == '-o': command.extend(['-o', str(output)]); i += 1
    else: command.append(word)
    i += 1
subprocess.run(command, check=True)
print(output)

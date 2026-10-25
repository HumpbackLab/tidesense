#!/usr/bin/env python3
"""Keep Schur observation algebra FP32 and protect the state covariance in FP64."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fp32_work', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
work = args.output.resolve()
src = work / 'src'
if src.exists():
    raise SystemExit(f'Refusing to overwrite {src}')
shutil.copytree(args.fp32_work.resolve() / 'src', src)
shutil.copytree(args.fp32_work.resolve() / 'share', work / 'share')
ov = src / 'third_party/open_vins'
shutil.copytree(ov / 'ov_msckf/src/state', ov / 'mixed/state')

# SLAM initialization has mutable matrix arguments. Marshal these explicitly;
# the tested mono/max_slam=0 path uses only the const-reference SV_EKF interface.
header = ov / 'mixed/state/StateHelper.h'
text = header.read_text()
old = '''  static bool initialize(std::shared_ptr<State> state, std::shared_ptr<ov_type::Type> new_variable,
                         const std::vector<std::shared_ptr<ov_type::Type>> &H_order, Eigen::MatrixXd &H_R, Eigen::MatrixXd &H_L,
                         Eigen::MatrixXd &R, Eigen::VectorXd &res, double chi_2_mult);'''
new = old + '''

  static bool initialize(std::shared_ptr<State> state, std::shared_ptr<ov_type::Type> new_variable,
                         const std::vector<std::shared_ptr<ov_type::Type>> &H_order, Eigen::MatrixXf &H_R, Eigen::MatrixXf &H_L,
                         Eigen::MatrixXf &R, Eigen::VectorXf &res, double chi_2_mult) {
    Eigen::MatrixXd H_R64 = H_R.cast<double>(), H_L64 = H_L.cast<double>(), R64 = R.cast<double>();
    Eigen::VectorXd res64 = res.cast<double>();
    bool result = initialize(state, new_variable, H_order, H_R64, H_L64, R64, res64, chi_2_mult);
    H_R = H_R64.cast<float>(); H_L = H_L64.cast<float>(); R = R64.cast<float>(); res = res64.cast<float>();
    return result;
  }'''
assert text.count(old) == 1
header.write_text(text.replace(old, new))

updater = ov / 'fp32/update/UpdaterSchurVINS.cpp'
text = updater.read_text()
old = 'StateHelper::SV_EKFUpdate(state, Hx_order, Amtx, Bvct, R_big);'
new = '''StateHelper::SV_EKFUpdate(state, Hx_order, Amtx.cast<double>(), Bvct.cast<double>(), R_big.cast<double>());'''
assert text.count(old) == 1
updater.write_text(text.replace(old, new))

cmake = src / 'benchmarks/openvins_replay/CMakeLists.txt'
text = cmake.read_text()
text = text.replace('option(OPENVINS_FP32_BACKEND ', 'option(OPENVINS_FP64_COVARIANCE "Protect covariance and propagation in FP64" ON)\noption(OPENVINS_FP32_BACKEND ', 1)
old = 'file(GLOB_RECURSE OV_CORE_SOURCES'
new = '''if(NOT OPENVINS_FP32_BACKEND OR NOT OPENVINS_FP64_COVARIANCE)
  message(FATAL_ERROR "This mixed source requires FP32 backend and FP64 covariance")
endif()
set(OPENVINS_STATE_ROOT "${OPENVINS_ROOT}/mixed/state")

file(GLOB_RECURSE OV_CORE_SOURCES'''
assert text.count(old) == 1
text = text.replace(old, new)
text = text.replace('${OPENVINS_BACKEND_ROOT}/state/', '${OPENVINS_STATE_ROOT}/')
text = text.replace('OPENVINS_BACKEND_PRECISION="fp32"', 'OPENVINS_BACKEND_PRECISION="fp32-schur-fp64-state"')
old = 'target_include_directories(openvins_full_native PUBLIC\n'
assert text.count(old) == 1
text = text.replace(old, old + '  "${OPENVINS_ROOT}/mixed"\n')
cmake.write_text(text)
manifest = {str(p.relative_to(src)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(src.rglob('*')) if p.is_file()}
(work / 'source-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(src)

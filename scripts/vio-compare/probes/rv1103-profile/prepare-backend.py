#!/usr/bin/env python3
"""Prepare a gate-preserving FP32 overlay, packed P H^T, and optional QR variants."""
from pathlib import Path
import argparse
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
src = args.output.resolve() / 'src'
if src.exists():
    raise SystemExit(f'Refusing to overwrite {src}')
shutil.copytree(args.baseline.resolve() / 'src', src)
ov = src / 'third_party/open_vins'
updater = ov / 'fp32/update/UpdaterMSCKF.cpp'
text = updater.read_text()
assert 'if (chi2 >' in text and 'chi2_check' in text
text = text.replace('using namespace ov_core;', '#include "utils/replay_profile.h"\nusing namespace ov_core;')
text = text.replace('  // Start timing', '  replay_profile::Chain profile(replay_profile::Clean);\n  // Start timing')
for number, stage in [(1, 'Triangulate'), (2, 'System'), (3, 'Compress'), (4, 'Ekf')]:
    old = f'  rT{number} = boost::posix_time::microsec_clock::local_time();'
    assert text.count(old) == 1
    text = text.replace(old, old + f'\n  profile.next(replay_profile::{stage});')
updater.write_text(text)

helper = ov / 'fp32/state/StateHelper.cpp'
text = helper.read_text()
start = text.index('void StateHelper::EKFUpdate(')
end = text.index('void StateHelper::set_initial_covariance(', start)
section = text[start:end]
old = '  // For each active variable find its M = P*H^T\n  for (const auto &var : state->_variables) {'
new = '''  // Optional packed product from the local backend-f32 experiment.
  // Covariance precision and the original EKF checks remain unchanged.
  const char *packed = std::getenv("OPENVINS_FP32_PACKED_MA");
  if (packed && std::strcmp(packed, "1") == 0) {
    Eigen::MatrixXf P_meas(state->_Cov.rows(), H.cols());
    for (size_t i = 0; i < H_order.size(); ++i) {
      P_meas.middleCols(H_id[i], H_order[i]->size()) =
          state->_Cov.middleCols(H_order[i]->id(), H_order[i]->size());
    }
    M_a.noalias() = P_meas * H.transpose();
  } else
  for (const auto &var : state->_variables) {'''
assert section.count(old) == 1
text = '#include <cstdlib>\n#include <cstring>\n' + text[:start] + section.replace(old, new) + text[end:]
helper.write_text(text)

# Port only the compression implementation from fpga_vio's ARM32 experiment;
# leave its unrelated outlier-gate deletions out. Keep Givens as the default.
old = '''  // Do measurement compression through givens rotations'''
new = '''  const char *compression = std::getenv("OPENVINS_COMPRESS");
  if (compression && std::strcmp(compression, "householder") == 0) {
    Eigen::HouseholderQR<Eigen::MatrixXf> qr(H_x);
    const int r = std::min(H_x.rows(), H_x.cols());
    res = (qr.householderQ().transpose() * res).eval();
    H_x = qr.matrixQR().topRows(r).triangularView<Eigen::Upper>();
    res.conservativeResize(r);
    return;
  }

  // Do measurement compression through givens rotations'''
for directory, scalar in [('fp32', 'MatrixXf'), ('ov_msckf/src', 'MatrixXd')]:
    helper = ov / directory / 'update/UpdaterHelper.cpp'
    text = helper.read_text()
    assert text.count(old) == 1
    replacement = new.replace('MatrixXf', scalar)
    helper.write_text('#include <cstdlib>\n#include <cstring>\n#include <Eigen/QR>\n' + text.replace(old, replacement))
print(src)

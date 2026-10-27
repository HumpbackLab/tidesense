#!/usr/bin/env python3
"""Generate an isolated ov-SchurVINS FP32 overlay and optional packed P H^T."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import textwrap

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
work = args.output.resolve()
src = work / 'src'
if src.exists():
    raise SystemExit(f'Refusing to overwrite {src}')
shutil.copytree(args.baseline.resolve() / 'src', src)
shutil.copytree(args.baseline.resolve() / 'share', work / 'share')
ov = src / 'third_party/open_vins'
scripts = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('openvins_fp32_transform', scripts.parent / 'backend-f32/prepare.py')
transform = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transform)

def replace_boundary(path, old, new):
    # The Schur fork uses different indentation at otherwise identical APIs.
    pattern = r'(?m)^' + r'\n'.join(r'[ \t]*' + re.escape(line.lstrip()) for line in old.splitlines())
    text = path.read_text()
    matches = list(re.finditer(pattern, text))
    assert len(matches) == 1, (path, len(matches))
    indent = re.match(r'[ \t]*', matches[0].group()).group()
    replacement = textwrap.indent(textwrap.dedent(new), indent)
    path.write_text(text[:matches[0].start()] + replacement + text[matches[0].end():])

# Compile original FP64 and FP32 overlay with the same optional packing code.
helper = ov / 'ov_msckf/src/state/StateHelper.cpp'
text = helper.read_text()
start = text.index('void StateHelper::SV_EKFUpdate(')
end = text.index('void StateHelper::set_initial_covariance(', start)
section = text[start:end]
old = '  for (const auto &var : state->_variables) {'
new = '''  const char *packed = std::getenv("OPENVINS_SCHUR_PACKED_MA");
  if (packed && std::strcmp(packed, "1") == 0) {
    Eigen::MatrixXd P_meas(state->_Cov.rows(), H.cols());
    for (size_t i = 0; i < H_order.size(); ++i) {
      P_meas.middleCols(H_id[i], H_order[i]->size()) =
          state->_Cov.middleCols(H_order[i]->id(), H_order[i]->size());
    }
    M_a.noalias() = P_meas * H.transpose();
  } else
  for (const auto &var : state->_variables) {'''
assert section.count(old) == 1
section = section.replace(old, new)
old = '''  state->_Cov.triangularView<Eigen::Upper>() -= K * M_a.transpose();
  state->_Cov = state->_Cov.selfadjointView<Eigen::Upper>();'''
new = '''  const char *covariance_form = std::getenv("OPENVINS_SCHUR_COVARIANCE");
  if (covariance_form && std::strcmp(covariance_form, "joseph") == 0) {
    Eigen::MatrixXd H_full = Eigen::MatrixXd::Zero(H.rows(), state_size);
    for (size_t i = 0; i < H_order.size(); ++i) {
      H_full.middleCols(H_order[i]->id(), H_order[i]->size()) =
          H.middleCols(H_id[i], H_order[i]->size());
    }
    Eigen::MatrixXd A = Eigen::MatrixXd::Identity(state_size, state_size) - K * H_full;
    Eigen::MatrixXd covariance_next = A * state->_Cov * A.transpose() + K * R * K.transpose();
    state->_Cov = (Eigen::MatrixXd::Scalar(0.5) * (covariance_next + covariance_next.transpose())).eval();
  } else {
    state->_Cov.triangularView<Eigen::Upper>() -= K * M_a.transpose();
    state->_Cov = state->_Cov.selfadjointView<Eigen::Upper>();
  }'''
assert section.count(old) == 1
section = section.replace(old, new)
old = '''  Eigen::MatrixXd S(R.rows(), R.rows());
  S = H * SV_releated_cov * H.transpose();
  S += R;'''
assert section.count(old) == 1
section = section.replace(old, '')
old = '''  Eigen::MatrixXd Sinv = Eigen::MatrixXd::Identity(R.rows(), R.rows());
  S.selfadjointView<Eigen::Upper>().llt().solveInPlace(Sinv);
  Eigen::MatrixXd K = M_a * Sinv.selfadjointView<Eigen::Upper>();'''
new = '''  Eigen::MatrixXd K;
  const char *solve_precision = std::getenv("OPENVINS_SCHUR_SOLVE");
  if (solve_precision && std::strcmp(solve_precision, "fp64") == 0) {
    // Named scalar deliberately remains double in the generated FP32 overlay.
    using SolveScalar = double;
    using SolveMatrix = Eigen::Matrix<SolveScalar, Eigen::Dynamic, Eigen::Dynamic>;
    const SolveMatrix H_solve = H.cast<SolveScalar>();
    const SolveMatrix P_solve = SV_releated_cov.cast<SolveScalar>();
    SolveMatrix S_solve = H_solve * P_solve * H_solve.transpose();
    S_solve += R.cast<SolveScalar>();
    SolveMatrix inverse = SolveMatrix::Identity(R.rows(), R.rows());
    S_solve.selfadjointView<Eigen::Upper>().llt().solveInPlace(inverse);
    K = (M_a.cast<SolveScalar>() * inverse.selfadjointView<Eigen::Upper>()).cast<Eigen::MatrixXd::Scalar>();
  } else {
    Eigen::MatrixXd S = H * SV_releated_cov * H.transpose();
    S += R;
    Eigen::MatrixXd Sinv = Eigen::MatrixXd::Identity(R.rows(), R.rows());
    S.selfadjointView<Eigen::Upper>().llt().solveInPlace(Sinv);
    K = M_a * Sinv.selfadjointView<Eigen::Upper>();
  }'''
assert section.count(old) == 1
section = section.replace(old, new)
helper.write_text('#include <cstdlib>\n#include <cstring>\n' + text[:start] + section + text[end:])

for directory in ['state', 'update']:
    shutil.copytree(ov / 'ov_msckf/src' / directory, ov / 'fp32' / directory)
    for path in (ov / 'fp32' / directory).rglob('*'):
        if path.suffix not in ['.cpp', '.h', '.hpp']:
            continue
        text = path.read_text()
        for old, new in transform.REPLACEMENTS:
            text = text.replace(old, new)
        path.write_text(text)

for name in ['UpdaterHelper.cpp', 'UpdaterSchurVINS.cpp']:
    replace_boundary(ov / 'fp32/update' / name, transform.JACOBIAN_OLD, transform.JACOBIAN_NEW)
for name in ['UpdaterMSCKF.cpp', 'UpdaterSLAM.cpp', 'UpdaterSchurVINS.cpp']:
    replace_boundary(ov / 'fp32/update' / name, transform.CLONE_OLD, transform.CLONE_NEW)
transform.replace_once(ov / 'fp32/state/StateHelper.cpp', transform.PROPAGATION_OLD, transform.PROPAGATION_NEW)
transform.replace_once(ov / 'fp32/state/StateHelper.cpp', transform.MARGINAL_OLD, transform.MARGINAL_NEW)

helper = ov / 'fp32/state/StateHelper.cpp'
text = helper.read_text()
old = '  if (found_neg) {\n    std::exit(EXIT_FAILURE);\n  }'
new = '''  if (found_neg) {
    if (const char *path = std::getenv("OPENVINS_SCHUR_FAILURE_LOG")) {
      if (FILE *file = std::fopen(path, "a")) {
        for (int i = 0; i < diags.rows(); ++i)
          if (diags(i) < 0) std::fprintf(file, "%s,%.17g,%d,%.17g\\n", __func__, state->_timestamp, i, double(diags(i)));
        std::fclose(file);
      }
    }
    std::exit(EXIT_FAILURE);
  }'''
assert text.count(old) == 3
helper.write_text('#include <cstdio>\n'+text.replace(old, new))

cmake = src / 'benchmarks/openvins_replay/CMakeLists.txt'
text = cmake.read_text()
old = '''if(OPENVINS_FP32_BACKEND)
  message(FATAL_ERROR "ov_SchurVINS FP32 adaptation is not implemented")
endif()'''
new = '''set(OPENVINS_BACKEND_ROOT "${OPENVINS_ROOT}/ov_msckf/src")
if(OPENVINS_FP32_BACKEND)
  set(OPENVINS_BACKEND_ROOT "${OPENVINS_ROOT}/fp32")
  if(NOT EXISTS "${OPENVINS_BACKEND_ROOT}/update/UpdaterSchurVINS.cpp")
    message(FATAL_ERROR "Prepared Schur FP32 overlay is missing")
  endif()
endif()'''
assert text.count(old) == 1
text = text.replace(old, new)
for directory in ['state', 'update']:
    text = text.replace('${OPENVINS_ROOT}/ov_msckf/src/'+directory+'/', '${OPENVINS_BACKEND_ROOT}/'+directory+'/')
old = 'target_include_directories(openvins_full_native PUBLIC\n'
assert text.count(old) == 1
text = text.replace(old, old+'  "${OPENVINS_BACKEND_ROOT}"\n')
cmake.write_text(text)

# Optional numerical diagnostics run after the timed VIO call, not inside it.
bench = src / 'benchmarks/openvins_replay/replay.cpp'
text = bench.read_text().replace('#include <chrono>', '#include <Eigen/Eigenvalues>\n#include <chrono>')
old = '  std::ofstream trajectory;'
new = '''  std::ofstream numeric;
  std::size_t numeric_frames = 0;
  if (const char *path = std::getenv("OPENVINS_SCHUR_NUMERIC_CSV")) {
    if (options.trajectory.empty()) throw std::runtime_error("numeric diagnostics require --trajectory");
    numeric.open(path);
    if (!numeric) throw std::runtime_error("cannot open numeric diagnostics");
    numeric << "camera_timestamp_ns,min_diagonal,max_asymmetry,min_eigenvalue_sampled\\n";
    numeric << std::setprecision(17);
  }
  std::ofstream trajectory;'''
assert text.count(old) == 1
text = text.replace(old, new)
old = '      trajectory << frame.timestamp_ns'
new = '''      if (!cov.allFinite() || !q.allFinite() || !p.allFinite() || !v.allFinite())
        throw std::runtime_error("non-finite per-frame state or covariance");
      if (numeric) {
        ++numeric_frames;
        numeric << frame.timestamp_ns << ',' << cov_min_diag << ',' << cov_asym_max << ',';
        if (numeric_frames == 1 || numeric_frames % 50 == 0 ||
            numeric_frames == static_cast<std::size_t>(options.samples + options.warmup)) {
          const Eigen::MatrixXd covariance64 = cov.cast<double>();
          Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance64);
          if (solver.info() != Eigen::Success) throw std::runtime_error("covariance eigenvalue diagnostic failed");
          numeric << solver.eigenvalues()(0);
        }
        numeric << '\\n';
        numeric.flush();
      }
      trajectory << frame.timestamp_ns'''
assert text.count(old) == 1
bench.write_text(text.replace(old, new))

manifest = {str(p.relative_to(src)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(src.rglob('*')) if p.is_file()}
(work / 'source-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(src)

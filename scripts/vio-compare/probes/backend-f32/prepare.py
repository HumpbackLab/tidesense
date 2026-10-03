#!/usr/bin/env python3
"""Generate an isolated OpenVINS FP32 backend from the maintained checkout.

The upstream v2.7 tree is intentionally left untouched.  Camera models,
timestamps, Ceres initialization, and nominal ov_type state storage remain
FP64.  For the fp32 variant, only ov_msckf/state and ov_msckf/update are
converted. Adapted from ~/fpga_vio/tools/prepare_openvins_fp32.py. The SDK's
existing headless option controls visualization; outlier gates remain intact.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path


TRANSFORM_VERSION = "tidesense-msckf-fp32-v3"

REPLACEMENTS = (
    ("Eigen::MatrixXd", "Eigen::MatrixXf"),
    ("Eigen::VectorXd", "Eigen::VectorXf"),
    ("Eigen::Matrix2d", "Eigen::Matrix2f"),
    ("Eigen::Matrix3d", "Eigen::Matrix3f"),
    ("Eigen::Matrix4d", "Eigen::Matrix4f"),
    ("Eigen::Vector2d", "Eigen::Vector2f"),
    ("Eigen::Vector3d", "Eigen::Vector3f"),
    ("Eigen::Vector4d", "Eigen::Vector4f"),
    ("Eigen::Quaterniond", "Eigen::Quaternionf"),
    ("Eigen::AngleAxisd", "Eigen::AngleAxisf"),
    ("Eigen::Matrix<double,", "Eigen::Matrix<float,"),
    ("cast<double>", "cast<float>"),
)

JACOBIAN_OLD = """      Eigen::MatrixXf dz_dzn, dz_dzeta;
      state->_cam_intrinsics_cameras.at(pair.first)->compute_distort_jacobian(uv_norm, dz_dzn, dz_dzeta);"""
JACOBIAN_NEW = """      Eigen::MatrixXd dz_dzn_fp64, dz_dzeta_fp64;
      state->_cam_intrinsics_cameras.at(pair.first)->compute_distort_jacobian(uv_norm, dz_dzn_fp64, dz_dzeta_fp64);
      Eigen::MatrixXf dz_dzn = dz_dzn_fp64.cast<float>();
      Eigen::MatrixXf dz_dzeta = dz_dzeta_fp64.cast<float>();"""

CLONE_OLD = """      clones_cami.insert({clone_imu.first, FeatureInitializer::ClonePose(R_GtoCi, p_CioinG)});"""
CLONE_NEW = """      const Eigen::Matrix3d R_GtoCi_fp64 = R_GtoCi.cast<double>();
      const Eigen::Vector3d p_CioinG_fp64 = p_CioinG.cast<double>();
      clones_cami.insert({clone_imu.first, FeatureInitializer::ClonePose(R_GtoCi_fp64, p_CioinG_fp64)});"""

PROPAGATION_OLD = """  state->_Cov.block(start_id, 0, phi_size, total_size) = Cov_PhiT.transpose();
  state->_Cov.block(0, start_id, total_size, phi_size) = Cov_PhiT;
  state->_Cov.block(start_id, start_id, phi_size, phi_size) = Phi_Cov_PhiT;"""
PROPAGATION_NEW = PROPAGATION_OLD + """
  state->_Cov = (0.5f * (state->_Cov + state->_Cov.transpose())).eval();"""

MARGINAL_OLD = """  state->_Cov = Cov_new;
  // state->Cov() = 0.5*(Cov_new+Cov_new.transpose());"""
MARGINAL_NEW = """  state->_Cov = Cov_new;
  state->_Cov = (0.5f * (state->_Cov + state->_Cov.transpose())).eval();
  // state->Cov() = 0.5*(Cov_new+Cov_new.transpose());"""

CMAKE_FP32 = """
option(OPENVINS_FP32_BACKEND "Compile the generated FP32 backend" OFF)
if (NOT OPENVINS_FP32_BACKEND)
    message(FATAL_ERROR "Generated FP32 source requires OPENVINS_FP32_BACKEND=ON")
endif ()
if (NOT EXISTS "${OPENVINS_FP32_EIGEN_HEADER}")
    message(FATAL_ERROR "OPENVINS_FP32_EIGEN_HEADER is missing")
endif ()
target_compile_options(ov_msckf_lib PUBLIC -include "${OPENVINS_FP32_EIGEN_HEADER}")
target_compile_definitions(ov_msckf_lib PUBLIC OPENVINS_FP32_BACKEND=1)
"""

PACKED_MA_OLD = """  // For each active variable find its M = P*H^T
  for (const auto &var : state->_variables) {"""
PACKED_MA_NEW = """  // Optional packed product, retaining the original FP32 path for A/B runs.
#ifdef OPENVINS_FP32_BACKEND
  const char *packed_ma = std::getenv("OPENVINS_FP32_PACKED_MA");
  if (packed_ma && std::strcmp(packed_ma, "1") == 0) {
    Eigen::MatrixXf P_meas(state->_Cov.rows(), H.cols());
    for (size_t i = 0; i < H_order.size(); ++i) {
      P_meas.middleCols(H_id[i], H_order[i]->size()) =
          state->_Cov.middleCols(H_order[i]->id(), H_order[i]->size());
    }
    M_a.noalias() = P_meas * H.transpose();
  } else
#endif
  for (const auto &var : state->_variables) {"""


def source_digest(source: Path, precision: str) -> str:
    digest = hashlib.sha256(f"{TRANSFORM_VERSION}:{precision}".encode())
    for directory in ("ov_core", "ov_init", "ov_msckf", "config"):
        for path in sorted((source / directory).rglob("*")):
            if not path.is_file():
                continue
            digest.update(path.relative_to(source).as_posix().encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one boundary pattern in {path}, found {count}")
    path.write_text(text.replace(old, new))


def transform_tree(source: Path, destination: Path, precision: str) -> None:
    signature = source_digest(source, precision)
    marker = destination / ".openvins_variant_signature"
    if marker.is_file() and marker.read_text().strip() == signature:
        return

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix="openvins_fp32_", dir=destination.parent))
    try:
        shutil.rmtree(temporary)
        shutil.copytree(source, temporary, ignore=shutil.ignore_patterns(".git"))
        if precision == "fp32":
            # A prior opt-in mixed build may have left its dormant patch in
            # the maintained source. Remove it only in the generated copy.
            helper = temporary / "ov_msckf/src/state/StateHelper.cpp"
            if "OPENVINS_EKF_MA_EXPERIMENT" in helper.read_text():
                patch = Path(__file__).resolve().parents[1] / "ekf-f32/openvins-core.patch"
                subprocess.run(["patch", "-R", "-p1", "-d", str(temporary)],
                               input=patch.read_bytes(), check=True, stdout=subprocess.DEVNULL)
            for directory in ("ov_msckf/src/state", "ov_msckf/src/update"):
                for path in (temporary / directory).rglob("*"):
                    if path.suffix not in {".h", ".hpp", ".cpp"}:
                        continue
                    text = path.read_text()
                    for old, new in REPLACEMENTS:
                        text = text.replace(old, new)
                    path.write_text(text)

            replace_once(
                temporary / "ov_msckf/src/update/UpdaterHelper.cpp",
                JACOBIAN_OLD,
                JACOBIAN_NEW,
            )
            for name in ("UpdaterMSCKF.cpp", "UpdaterSLAM.cpp"):
                replace_once(
                    temporary / "ov_msckf/src/update" / name,
                    CLONE_OLD,
                    CLONE_NEW,
                )
            replace_once(
                temporary / "ov_msckf/src/state/StateHelper.cpp",
                PROPAGATION_OLD,
                PROPAGATION_NEW,
            )
            replace_once(
                temporary / "ov_msckf/src/state/StateHelper.cpp",
                MARGINAL_OLD,
                MARGINAL_NEW,
            )
            helper = temporary / "ov_msckf/src/state/StateHelper.cpp"
            text = helper.read_text()
            start = text.index("void StateHelper::EKFUpdate(")
            end = text.index("void StateHelper::set_initial_covariance(", start)
            section = text[start:end]
            if section.count(PACKED_MA_OLD) != 1:
                raise RuntimeError("expected original EKF M_a loop")
            text = text[:start] + section.replace(PACKED_MA_OLD, PACKED_MA_NEW) + text[end:]
            text = text.replace("#include <boost/math/distributions/chi_squared.hpp>",
                                "#include <boost/math/distributions/chi_squared.hpp>\n#include <cstdlib>\n#include <cstring>")
            helper.write_text(text)
            cmake = temporary / "ov_msckf/CMakeLists.txt"
            cmake.write_text(cmake.read_text() + CMAKE_FP32)
            # Both original gates must survive conversion.
            for name in ("UpdaterMSCKF.cpp", "UpdaterSLAM.cpp"):
                text = (temporary / "ov_msckf/src/update" / name).read_text()
                if "chi2 >" not in text or "chi2_check" not in text:
                    raise RuntimeError(f"outlier gate missing in {name}; use a source with original rejection")
        marker_in_temporary = temporary / marker.name
        marker_in_temporary.write_text(signature + "\n")
        if destination.exists():
            # Retain timestamps for unchanged outputs so an incremental build
            # recompiles only files whose generated contents actually changed.
            for path in temporary.rglob("*"):
                old = destination / path.relative_to(temporary)
                if path.is_file() and old.is_file() and path.read_bytes() == old.read_bytes():
                    shutil.copystat(old, path)
            shutil.rmtree(destination)
        temporary.rename(destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--precision", choices=("fp64", "fp32"), default="fp32")
    args = parser.parse_args()
    source = args.source.resolve()
    destination = args.output.resolve()
    if destination == source or source in destination.parents:
        raise SystemExit("output must be outside the input source tree")
    if not (source / "ov_msckf/src/state/State.cpp").is_file():
        raise SystemExit(f"not an OpenVINS checkout: {source}")
    transform_tree(source, destination, args.precision)


if __name__ == "__main__":
    main()

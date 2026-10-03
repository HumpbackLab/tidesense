#pragma once

#include <Eigen/Eigenvalues>
#include <cstdlib>
#include <limits>
#include <ostream>
#include <stdexcept>
#include "state/StateHelper.h"

// Probe runs outside the timed camera call, identically in all precisions.
inline void append_numeric_probe(std::ostream &out, const std::shared_ptr<ov_msckf::State> &state, int frame) {
  const auto covariance = ov_msckf::StateHelper::get_full_covariance(state);
  if (!covariance.allFinite() || covariance.diagonal().minCoeff() < 0)
    throw std::runtime_error("nonfinite or negative covariance diagonal");
  const double asym = (covariance - covariance.transpose()).cwiseAbs().maxCoeff();
  out << ',' << covariance.rows() << ',' << covariance.diagonal().minCoeff() << ',' << asym << ',';
  if (std::getenv("OPENVINS_NUMERIC_EIGEN") && frame % 50 == 0) {
    const Eigen::MatrixXd covariance_double = covariance.template cast<double>();
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solve(covariance_double, Eigen::EigenvaluesOnly);
    if (solve.info() != Eigen::Success) throw std::runtime_error("covariance eigenvalue probe failed");
    out << solve.eigenvalues().minCoeff();
  }
}

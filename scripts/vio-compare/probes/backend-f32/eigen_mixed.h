#pragma once

#include <Eigen/Core>

// OpenVINS v2.7 hard-codes double at the camera, initializer, and nominal
// state interfaces.  The experimental backend keeps those interfaces in
// FP64, while the generated MSCKF propagation/update sources use FP32.
// Eigen normally rejects mixed scalar expressions, so define the deliberate
// narrowing rule at those boundaries.  The generated source adds explicit
// casts where a non-const output reference crosses the boundary.
namespace Eigen {

template <typename BinaryOp>
struct ScalarBinaryOpTraits<float, double, BinaryOp> {
  using ReturnType = float;
};

template <typename BinaryOp>
struct ScalarBinaryOpTraits<double, float, BinaryOp> {
  using ReturnType = float;
};

}  // namespace Eigen

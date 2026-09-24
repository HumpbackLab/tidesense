// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "live_imu.hpp"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <functional>
#include <memory>

// All state is in the backend world/body convention. Timestamp is MONOTONIC ns.
struct PredictedState {
  uint64_t stamp_ns = 0;
  Eigen::Quaternion<double, Eigen::DontAlign> q = Eigen::Quaterniond::Identity();
  Eigen::Vector3d p = Eigen::Vector3d::Zero(), v = Eigen::Vector3d::Zero();
  Eigen::Vector3d ba = Eigen::Vector3d::Zero(), bg = Eigen::Vector3d::Zero();
  Eigen::Vector3d gravity = Eigen::Vector3d(0, 0, -9.80665);
};

// Independent propagation state; never reads/mutates the visual backend on its worker.
// Live input uses a bounded event queue; synchronous mode enables deterministic replay/tests.
class ImuPrediction {
 public:
  using Sink = std::function<void(const PredictedState&, bool)>;
  explicit ImuPrediction(Sink sink, bool threaded = true, uint64_t max_age_ns = 500000000);
  ~ImuPrediction();
  void sample(const ImuSample& sample);
  void correct(const PredictedState& state);
  void invalidate();
  void check() const;
  void finish();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

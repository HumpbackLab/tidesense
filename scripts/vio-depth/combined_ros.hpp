// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "depth.hpp"
#include <array>
#include <memory>
struct VisualPose {
  bool valid=false;
  std::array<double,3> p{};
  std::array<double,4> q{{0,0,0,1}}; // x,y,z,w; T_W_B, body/IMU origin in odom.
};
// One micro-ROS context, three publishers. Each submission is an immutable join
// of the pose and depth computed from the same capture, sharing a header stamp.
class CombinedRos {
 public:
  explicit CombinedRos(const DepthIntrinsics& info);
  ~CombinedRos();
  bool submit(uint64_t stamp,const cv::Mat& depth,const VisualPose& pose);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

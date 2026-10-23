// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "depth.hpp"
#include <cstdint>
#include <memory>

// One ROS owner thread, with a single pending depth frame. No acquisition stalls.
class DepthRos {
 public:
  explicit DepthRos(const DepthIntrinsics& info);
  ~DepthRos();
  bool submit(uint64_t ros_ns, const cv::Mat& depth);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

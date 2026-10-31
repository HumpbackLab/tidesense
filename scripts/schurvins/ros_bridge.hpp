// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>

// Build-selected ROS transport. No ROS types or network work escape this boundary.
class RosBridge {
 public:
  // Native DDS defaults on; SCHUR_ROS=0 disables it. micro-ROS remains opt-in.
  RosBridge();
  ~RosBridge();
  RosBridge(const RosBridge&)=delete;
  RosBridge& operator=(const RosBridge&)=delete;
  static bool compiled();
  static const char* backend();
  bool enabled() const;
  // T_odom_body, quaternion xyzw; acquisition time mapped by the caller.
  // A nontracking update clears queued poses and publishes tracking=false only.
  void submit(uint64_t timestamp_ns,bool tracking,const double* p=nullptr,const double* q=nullptr);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

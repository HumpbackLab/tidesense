// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>

// No ROS types escape this interface. Only the communication thread calls rcl/rmw.
class MicroRosBridge {
 public:
  // Runtime opt-in: SCHUR_MICRO_ROS=1. Off creates no thread or ROS context.
  explicit MicroRosBridge(bool calibration = false);
  ~MicroRosBridge();
  MicroRosBridge(const MicroRosBridge&) = delete;
  MicroRosBridge& operator=(const MicroRosBridge&) = delete;
  static bool compiled();
  bool enabled() const;
  // timestamp_ns is acquisition/dataset time, not transmission time.
  // Pose is T_odom_body; p xyz, q xyzw. Nontracking updates never publish a pose.
  void submit(uint64_t timestamp_ns, bool tracking, const double* p = nullptr,
              const double* q = nullptr);
  // Calibration-only, continuous mono8 640x400 buffers; copied before returning.
  // Both images use the pair's left VI acquisition stamp. Individual VI stamps
  // are retained in the calibration CSV, not treated as exposure timestamps.
  bool submitImages(uint64_t timestamp_ns, const uint8_t* left, const uint8_t* right);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

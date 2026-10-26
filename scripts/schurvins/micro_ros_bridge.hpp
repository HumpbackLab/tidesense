// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>

// No ROS types escape this interface. Only the communication thread calls rcl/rmw.
class MicroRosBridge {
 public:
  // Runtime opt-in: SCHUR_ROS=1 (or legacy SCHUR_MICRO_ROS=1). Off creates no thread/context.
  explicit MicroRosBridge(bool calibration = false, bool imu = false);
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
  // Calibration IMU: bounded FIFO; failure is fatal rather than silently dropping samples.
  bool submitImu(uint64_t timestamp_ns, const double* gyro, const double* accel);
  void finishImu(); // Wait for queued IMU publication (bounded timeout).
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

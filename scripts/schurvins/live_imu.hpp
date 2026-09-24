#pragma once
#include <array>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sched.h>
#include <sys/types.h>
#include "accel_calibration.hpp"
struct ImuSample {
 uint64_t stamp_ns;
 std::array<double,3> raw_accel{}; // SI before six-face correction, for recording diagnostics
 std::array<double,3> gyro, accel; // rad/s, m/s^2; chip axes, accel six-face correction applied
};
// Unlike images, every IMU sample is retained until delivered to the estimator.
class LiveImu {
 public:
  explicit LiveImu(const std::string& device, std::function<void(const ImuSample&)> on_sample = {});
  ~LiveImu();
  std::vector<ImuSample> through(uint64_t image_ns, const volatile sig_atomic_t& interrupted);
  void stop();
  void report() const;
  void check() const;
  YAML::Node recordingMetadata() const;
 private:
  const std::function<void(const ImuSample&)> on_sample_;
  void acquire();
  void setting(const std::string& name, const std::string& value);
  void powerDown() noexcept;
  int fd_ = -1;
  bool enabled_ = false;
  std::string sysfs_;
  double accel_scale_ = 0, gyro_scale_ = 0;
  AccelCalibration accel_calibration_;
  std::string calibration_path_;
  std::atomic<bool> stopping_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<ImuSample> queue_;
  std::string error_;
  uint64_t samples_ = 0, delivered_ = 0, first_ = 0, last_ = 0, max_gap_ = 0, late_ = 0;
  size_t high_water_ = 0;
};

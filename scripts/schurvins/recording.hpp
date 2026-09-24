#pragma once
#include "live_camera.hpp"
#include "live_imu.hpp"
#include <deque>
#include <fstream>

// Records estimator input, not frames replaced in LiveCamera's latest-frame queue.
// A bounded background writer owns disk I/O. Overflow is an error, never a silent drop.
class VioRecording {
 public:
  VioRecording(const std::string& directory, const YAML::Node& calibration,
               const std::string& sensor_ini);
  ~VioRecording();
  void setImuMetadata(const YAML::Node& metadata);
  void submit(uint64_t timestamp, const LivePair& pair, const std::vector<ImuSample>& imu);
  void finish();
 private:
  struct Batch { uint64_t timestamp; LivePair pair; std::vector<ImuSample> imu; };
  void writer();
  void write(const Batch& batch);
  void join();
  void checkSpace() const;
  void summary(bool complete);
  std::string dir_, error_;
  std::ofstream stereo_, imu_, raw_imu_, frames_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Batch> queue_;
  std::thread worker_;
  bool stopping_ = false, finished_ = false;
  const size_t capacity_;
  size_t high_water_ = 0;
  uint64_t pairs_ = 0, samples_ = 0, first_ = 0, last_ = 0;
};

#include "recording.hpp"
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
size_t recordingCapacity() {
  const char* value = std::getenv("SCHUR_RECORD_QUEUE_PAIRS");
  if (!value) return 32;
  size_t count = 0;
  for (const char* p = value; *p; ++p) {
    if (*p < '0' || *p > '9' || count > 128)
      throw std::runtime_error("SCHUR_RECORD_QUEUE_PAIRS must be an integer from 1 to 128");
    count = count * 10 + (*p - '0');
  }
  if (!count || count > 128)
    throw std::runtime_error("SCHUR_RECORD_QUEUE_PAIRS must be an integer from 1 to 128");
  return count;
}
void directory(const std::string& path) {
  if (::mkdir(path.c_str(), 0755))
    throw std::runtime_error("Cannot create recording directory (must be new): " + path + ": " + std::strerror(errno));
}
void open(std::ofstream& stream, const std::string& path, bool binary = false) {
  stream.exceptions(std::ios::failbit | std::ios::badbit);
  stream.open(path, std::ios::out | (binary ? std::ios::binary : std::ios::openmode(0)));
  stream << std::setprecision(17);
}
void yamlFile(const std::string& path, const YAML::Node& node) {
  YAML::Emitter emitter; emitter.SetDoublePrecision(17); emitter << node;
  if (!emitter.good()) throw std::runtime_error("Cannot serialize recording metadata");
  std::ofstream stream; open(stream, path); stream << emitter.c_str() << '\n'; stream.close();
}
void pgm(const std::string& path, const cv::Mat& image) {
  if (image.empty() || image.type() != CV_8UC1) throw std::runtime_error("Recording requires mono8 images");
  std::ofstream stream; open(stream, path + ".tmp", true);
  stream << "P5\n" << image.cols << ' ' << image.rows << "\n255\n";
  for (int row = 0; row < image.rows; ++row)
    stream.write(reinterpret_cast<const char*>(image.ptr(row)), image.cols);
  stream.close();
  if (::rename((path + ".tmp").c_str(), path.c_str())) throw std::runtime_error("Cannot commit recorded image");
}
}

VioRecording::VioRecording(const std::string& dir, const YAML::Node& calibration,
                           const std::string& ini) : dir_(dir), capacity_(recordingCapacity()) {
  directory(dir_); directory(dir_ + "/left"); directory(dir_ + "/right");
  { std::ofstream flag; open(flag, dir_ + "/INCOMPLETE"); flag << "Recording not closed successfully\n"; }
  checkSpace();
  yamlFile(dir_ + "/calibration.yaml", calibration);
  YAML::Node replay = YAML::Clone(calibration);
  replay["live_camera_time_offset_ns"] = 0;
  replay["imu_params"]["delay_imu_cam"] = 0.0;
  yamlFile(dir_ + "/replay.yaml", replay);
  std::ifstream sensor(ini, std::ios::binary);
  if (!sensor) throw std::runtime_error("Cannot snapshot sensor INI");
  std::ofstream sensor_copy; open(sensor_copy, dir_ + "/sensor_cfg.ini", true);
  sensor_copy << sensor.rdbuf(); sensor_copy.close();
  open(stereo_, dir_ + "/stereo.csv"); open(imu_, dir_ + "/imu.csv");
  open(frames_, dir_ + "/frames.csv");
  stereo_ << "#timestamp_ns,left_image,right_image\n";
  imu_ << "#timestamp_ns,gx,gy,gz,ax,ay,az; nominal raw SI; replay applies IMU intrinsics once\n";
  frames_ << "#timestamp_ns,sequence,left_ns,right_ns,left_ref,right_ref,flags,imu_count,imu_last_ns\n";
  summary(false);
  std::cerr << "recording: queue_capacity=" << capacity_ << " pairs\n";
  worker_ = std::thread(&VioRecording::writer, this);
}
VioRecording::~VioRecording() {
  if (!finished_) {
    join();
    try { summary(false); } catch (...) {}
    std::cerr << "recording: incomplete (inspect INCOMPLETE and metadata.yaml): " << dir_ << '\n';
  }
}
void VioRecording::setImuMetadata(const YAML::Node& metadata) {
  yamlFile(dir_ + "/imu-source.yaml", metadata);
}
void VioRecording::checkSpace() const {
  struct statvfs fs{};
  if (statvfs(dir_.c_str(), &fs)) throw std::runtime_error("Cannot check recording disk space");
  if (uint64_t(fs.f_bavail)*fs.f_frsize < 32ULL*1024*1024)
    throw std::runtime_error("Recording requires at least 32 MiB free disk space");
  if (fs.f_flag & ST_SYNCHRONOUS)
    throw std::runtime_error("Recording filesystem is mounted sync; remount async first");
}
void VioRecording::submit(uint64_t timestamp, const LivePair& pair, const std::vector<ImuSample>& imu) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!error_.empty()) throw std::runtime_error("Recording: " + error_);
  if (stopping_) throw std::runtime_error("Recording already stopped");
  if (queue_.size() >= capacity_) {
    error_ = "queue full: SD cannot keep up; recording is incomplete";
    throw std::runtime_error("Recording " + error_);
  }
  Batch batch{timestamp, pair, imu};
  batch.pair.left = pair.left.clone(); batch.pair.right = pair.right.clone();
  queue_.push_back(std::move(batch));
  high_water_ = std::max(high_water_, queue_.size());
  ready_.notify_one();
}
void VioRecording::writer() {
  // Linux nice is per-thread: keep estimator scheduling ahead of image writes.
  setpriority(PRIO_PROCESS, 0, 5);
  try {
    while (true) {
      Batch batch;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
        if (queue_.empty()) break;
        batch = std::move(queue_.front()); queue_.pop_front();
      }
      write(batch);
    }
  } catch (const std::exception& e) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = e.what(); queue_.clear();
  }
}
void VioRecording::write(const Batch& b) {
  checkSpace();
  if (b.timestamp <= last_ || b.imu.empty() || b.imu.back().stamp_ns <= b.timestamp)
    throw std::runtime_error("Invalid recording timestamps/IMU coverage");
  const std::string left = "left/" + std::to_string(b.timestamp) + ".pgm";
  const std::string right = "right/" + std::to_string(b.timestamp) + ".pgm";
  pgm(dir_ + "/" + left, b.pair.left); pgm(dir_ + "/" + right, b.pair.right);
  for (const auto& s : b.imu) {
    imu_ << s.stamp_ns;
    for (double v : s.gyro) imu_ << ',' << v;
    for (double v : s.accel) imu_ << ',' << v;
    imu_ << '\n';
  }
  imu_.flush();
  const auto& h = b.pair.header;
  frames_ << b.timestamp << ',' << h.sequence << ',' << h.left_ns << ',' << h.right_ns
          << ',' << h.left_ref << ',' << h.right_ref << ',' << h.flags << ',' << b.imu.size()
          << ',' << b.imu.back().stamp_ns << '\n';
  frames_.flush();
  // Commit the replay index last; it only references fully written images and IMU.
  stereo_ << b.timestamp << ',' << left << ',' << right << '\n'; stereo_.flush();
  if (!pairs_) first_ = b.timestamp;
  last_ = b.timestamp; ++pairs_; samples_ += b.imu.size();
}
void VioRecording::join() {
  { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; ready_.notify_one(); }
  if (worker_.joinable()) worker_.join();
}
void VioRecording::summary(bool complete) {
  YAML::Node n;
  n["schema_version"] = 2; n["complete"] = complete;
  n["scope"] = "VIO-consumed pairs and IMU, excluding startup/replaced camera frames and IMU after last pair";
  n["clock"] = "CLOCK_MONOTONIC nanoseconds; stereo.csv includes cam0 time offset";
  n["images"] = "mono8 PGM, original orientation, no rectification";
  n["imu_value_domain"] = "raw_si";
  n["imu"] = "imu.csv: nominal raw SI; intrinsics are stored in replay.yaml and applied once";
  n["pairs"] = pairs_; n["imu_samples"] = samples_;
  n["first_timestamp_ns"] = first_; n["last_timestamp_ns"] = last_;
  n["queue_capacity"] = capacity_; n["queue_high_water"] = high_water_;
  n["writer_error"] = error_;
  yamlFile(dir_ + "/metadata.yaml", n);
}
void VioRecording::finish() {
  join();
  if (!error_.empty()) throw std::runtime_error("Recording: " + error_);
  if (!pairs_) throw std::runtime_error("Recording contains no frames");
  stereo_.close(); imu_.close(); frames_.close();
  summary(true);
  { std::ofstream flag; open(flag, dir_ + "/INCOMPLETE"); flag << "Recording closed successfully\n"; flag.close(); }
  if (::rename((dir_ + "/INCOMPLETE").c_str(), (dir_ + "/COMPLETE").c_str()))
    throw std::runtime_error("Cannot finalize recording");
  finished_ = true;
  std::cerr << "recording: complete pairs=" << pairs_ << " imu=" << samples_
            << " queue_high_water=" << high_water_ << " directory=" << dir_ << '\n';
}

// SPDX-License-Identifier: BSD-3-Clause
// Consume SC7U22 IIO scans. Sensor configuration and timestamps belong to kernel.
#include "live_imu.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <poll.h>
#include <unistd.h>
#include <limits.h>
namespace {
std::string value(const std::string& path) {
 std::ifstream in(path); std::string result;
 if (!(in >> result)) throw std::runtime_error("Cannot read " + path);
 return result;
}
int16_t le16(const uint8_t* p) { return int16_t(uint16_t(p[0]) | uint16_t(p[1])<<8); }
uint64_t le64(const uint8_t* p) {
 uint64_t v=0; for (int i=7;i>=0;--i) v=(v<<8)|p[i]; return v;
}
}
void LiveImu::setting(const std::string& name, const std::string& text) {
 std::ofstream out(sysfs_ + "/" + name);
 out << text; out.flush();
 if (!out) throw std::runtime_error("Cannot configure IIO " + name);
}
LiveImu::LiveImu(const std::string& requested, std::function<void(const ImuSample&)> callback)
 : on_sample_(std::move(callback)) {
 std::string device=requested;
 if (device == "auto") {
  const char* resolved=std::getenv("SCHUR_IMU_DEVICE");
  if (!resolved) throw std::runtime_error("Use run.sh with auto, or specify /dev/iio:deviceN");
  device=resolved;
 }
 fd_=open(device.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
 if (fd_<0) throw std::runtime_error("Cannot open IMU " + device + ": " + strerror(errno));
 try {
  if (flock(fd_,LOCK_EX|LOCK_NB)) throw std::runtime_error("IMU is owned by another process");
  struct stat st{}; char path[PATH_MAX];
  if (fstat(fd_,&st) || !S_ISCHR(st.st_mode)) throw std::runtime_error("Expected IIO character device");
  std::string link="/sys/dev/char/"+std::to_string(major(st.st_rdev))+":"+std::to_string(minor(st.st_rdev));
  if (!realpath(link.c_str(),path)) throw std::runtime_error("Cannot resolve IIO sysfs");
  sysfs_=path;
  if (value(sysfs_+"/name")!="sc7u22") throw std::runtime_error("Expected SC7U22 IIO driver; spidev polling is no longer supported");
  if (value(sysfs_+"/buffer/enable")!="0") throw std::runtime_error("SC7U22 IIO buffer already enabled");
  setting("current_timestamp_clock","monotonic");
  const char* channels[]={"in_accel_x","in_accel_y","in_accel_z","in_anglvel_x","in_anglvel_y","in_anglvel_z","in_timestamp"};
  for (int i=0;i<7;++i) {
   std::string prefix="scan_elements/"+std::string(channels[i]);
   if (value(sysfs_+"/"+prefix+"_index")!=std::to_string(i) ||
       value(sysfs_+"/"+prefix+"_type")!=(i==6?"le:s64/64>>0":"le:s16/16>>0"))
    throw std::runtime_error("Unexpected SC7U22 IIO scan layout");
   setting(prefix+"_en","1");
  }
  if (value(sysfs_+"/in_accel_sampling_frequency")!="200" ||
      value(sysfs_+"/in_anglvel_sampling_frequency")!="200")
   throw std::runtime_error("SC7U22 must sample at 200 Hz");
  accel_scale_=std::stod(value(sysfs_+"/in_accel_scale"));
  gyro_scale_=std::stod(value(sysfs_+"/in_anglvel_scale"));
  const char* configured=std::getenv("SCHUR_IMU_CALIBRATION");
  const std::string calibration=configured ? configured :
    "/mnt/data/penguin-flight-console/accelerometer-calibration.json";
  calibration_path_=calibration;
  if (calibration == "none") {
   std::cerr << "imu: accelerometer calibration explicitly DISABLED; raw SI values\n";
  } else {
   try {
    accel_calibration_=AccelCalibration::parse(YAML::LoadFile(calibration),accel_scale_);
   } catch (const std::exception& e) {
    throw std::runtime_error("Cannot load accelerometer calibration " + calibration + ": " + e.what());
   }
   std::cerr << "imu: six-face accelerometer calibration applied: " << calibration << '\n';
   for (int i=0;i<3;++i)
    std::cerr << "imu: axis=" << i << " bias_m_s2=" << accel_calibration_.bias[i]
              << " correction_scale=" << accel_calibration_.scale[i] << '\n';
  }
  setting("buffer/length","512"); setting("buffer/watermark","1");
  setting("buffer/enable","1"); enabled_=true;
  std::cerr << "imu: SC7U22 " << device << " IIO 200Hz, INT1 hard-IRQ CLOCK_MONOTONIC timestamps\n";
  worker_=std::thread(&LiveImu::acquire,this);
 } catch (...) { powerDown(); close(fd_); fd_=-1; throw; }
}
void LiveImu::acquire() {
 try {
  while (!stopping_) {
   pollfd p{fd_,POLLIN,0};
   int ready=poll(&p,1,100); // Wait for kernel-buffered data, never poll sensor registers.
   if (ready<0 && errno==EINTR) continue;
   if (stopping_) break;
   if (ready<=0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL)))
    throw std::runtime_error("IMU IIO interrupt stream stalled or failed");
   uint8_t scans[24*32];
   ssize_t bytes=::read(fd_,scans,sizeof(scans));
   if (bytes<0 && (errno==EAGAIN || errno==EINTR)) continue;
   if (bytes<=0 || bytes%24) throw std::runtime_error("Invalid SC7U22 IIO scan read");
   for (ssize_t n=0;n<bytes;n+=24) {
    ImuSample sample{}; sample.stamp_ns=le64(scans+n+16);
    for (int axis=0;axis<3;++axis) {
     const int16_t a=le16(scans+n+axis*2),g=le16(scans+n+6+axis*2);
     if (a>=32760 || a<=-32760 || g>=32760 || g<=-32760)
      throw std::runtime_error("IMU raw sample saturated");
     sample.raw_accel[axis]=a*accel_scale_;
     sample.accel[axis]=accel_calibration_.apply(axis,sample.raw_accel[axis]);
     sample.gyro[axis]=g*gyro_scale_;
    }
    // Queue prediction before making this sample visible to the visual consumer.
    if (on_sample_) on_sample_(sample);
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_) {
     uint64_t gap=sample.stamp_ns-last_;
     if (sample.stamp_ns<=last_ || gap>=15000000)
      throw std::runtime_error("IMU sample timing gap reaches SchurVINS 15ms limit; refusing invalid integration");
     max_gap_=std::max(max_gap_,gap); if (gap>7500000) ++late_;
    } else first_=sample.stamp_ns;
    if (queue_.size()>=2000) throw std::runtime_error("IMU history full (10s)");
    queue_.push_back(sample); last_=sample.stamp_ns; ++samples_;
    high_water_=std::max(high_water_,queue_.size()); ready_.notify_one();
   }
  }
 } catch (const std::exception& e) {
  std::lock_guard<std::mutex> lock(mutex_); error_=e.what(); ready_.notify_all();
 }
}
std::vector<ImuSample> LiveImu::through(uint64_t image_ns, const volatile sig_atomic_t& interrupted) {
 std::unique_lock<std::mutex> lock(mutex_);
 auto end = std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
 while ((queue_.empty() || queue_.back().stamp_ns <= image_ns) && error_.empty() && !interrupted && !stopping_) {
  if (ready_.wait_until(lock,end) == std::cv_status::timeout)
   throw std::runtime_error("IMU does not bracket the camera timestamp within 200ms");
 }
 if (!error_.empty()) throw std::runtime_error(error_);
 if (interrupted || stopping_) return {};
 std::vector<ImuSample> samples;
 while (!queue_.empty()) {
  samples.push_back(queue_.front()); queue_.pop_front(); ++delivered_;
  if (samples.back().stamp_ns > image_ns) break; // include the first sample after the image
 }
 return samples;
}
void LiveImu::check() const {
 std::lock_guard<std::mutex> lock(mutex_);
 if (!error_.empty()) throw std::runtime_error(error_);
}
void LiveImu::powerDown() noexcept {
 if (!enabled_) return;
 try { setting("buffer/enable", "0"); }
 catch (const std::exception& e) { std::cerr << "imu: buffer disable failed: " << e.what() << '\n'; }
 enabled_ = false;
}
void LiveImu::stop() {
 stopping_ = true; ready_.notify_all();
 if (worker_.joinable()) worker_.join();
 powerDown();
 if (fd_ >= 0) { close(fd_); fd_ = -1; }
}
LiveImu::~LiveImu() { stop(); }
void LiveImu::report() const {
 std::lock_guard<std::mutex> lock(mutex_);
 std::cout << "imu_samples=" << samples_ << " imu_delivered=" << delivered_
  << " imu_rate_hz=" << (last_>first_ ? (samples_-1)*1e9/(last_-first_) : 0)
  << " imu_max_gap_ms=" << max_gap_*1e-6 << " imu_late_intervals=" << late_
  << " imu_queue_high_water=" << high_water_ << " imu_discarded=0\n";
}

YAML::Node LiveImu::recordingMetadata() const {
 YAML::Node n;
 n["sensor"]="sc7u22"; n["frame"]="imu_chip_xyz";
 n["sampling_frequency_hz"]=200;
 n["source"]=calibration_path_;
 n["six_face_applied"]=calibration_path_!="none";
 n["accel_scale_m_s2_per_lsb"]=accel_scale_;
 n["gyro_scale_rad_s_per_lsb"]=gyro_scale_;
 for (int i=0;i<3;++i) {
  n["bias_m_s2"].push_back(accel_calibration_.bias[i]);
  n["correction_scale"].push_back(accel_calibration_.scale[i]);
 }
 return n;
}

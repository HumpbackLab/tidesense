// SPDX-License-Identifier: BSD-3-Clause
// Initialization/SPI protocol adapted from scripts/sc7u22/sc7u22_test.c and PX4
// src/drivers/imu/silan/sc7u22. See LICENSE.PX4-SC7U22.
#include "live_imu.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <fstream>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
namespace {
uint64_t monotonicNs() {
 timespec t;
 if (clock_gettime(CLOCK_MONOTONIC, &t)) throw std::runtime_error("IMU clock_gettime failed");
 return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
int16_t be16(const uint8_t *p) { return int16_t((uint16_t(p[0])<<8)|p[1]); }
void delay(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}
void LiveImu::prime() {
 // On this board, the first SPI transaction after an idle interval can return
 // zero. A harmless WHO_AM_I transaction precedes each register operation.
 uint8_t tx[2]{0x81,0}, rx[2]{};
 spi_ioc_transfer transfer{};
 transfer.tx_buf = reinterpret_cast<uintptr_t>(tx); transfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
 transfer.len = 2; transfer.speed_hz = 1000000; transfer.bits_per_word = 8;
 if (ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) != 2)
  throw std::runtime_error("IMU SPI priming transaction failed");
}
void LiveImu::read(uint8_t reg, uint8_t *data, size_t size) {
 prime();
 if (size > 15) throw std::runtime_error("IMU SPI burst too large");
 uint8_t tx[16]{}, rx[16]{}; tx[0] = reg | 0x80;
 spi_ioc_transfer transfer{};
 transfer.tx_buf = reinterpret_cast<uintptr_t>(tx); transfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
 transfer.len = size+1; transfer.speed_hz = 1000000; transfer.bits_per_word = 8;
 if (ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) != int(size+1))
  throw std::runtime_error("IMU SPI read failed: "+std::string(strerror(errno)));
 std::memcpy(data, rx+1, size);
}
void LiveImu::write(uint8_t reg, uint8_t value) {
 prime();
 uint8_t tx[2]{uint8_t(reg & 0x7f), value}, rx[2]{};
 spi_ioc_transfer transfer{};
 transfer.tx_buf = reinterpret_cast<uintptr_t>(tx); transfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
 transfer.len = 2; transfer.speed_hz = 1000000; transfer.bits_per_word = 8;
 if (ioctl(fd_, SPI_IOC_MESSAGE(1), &transfer) != 2)
  throw std::runtime_error("IMU SPI write failed: "+std::string(strerror(errno)));
 delay(1);
}
void LiveImu::expect(uint8_t reg, uint8_t value) {
 uint8_t got; read(reg, &got, 1);
 if (got != value) throw std::runtime_error("IMU register readback mismatch at "+std::to_string(reg)+": expected "+std::to_string(value)+", got "+std::to_string(got));
}
LiveImu::LiveImu(const std::string& requested) {
 std::string device = requested;
 if (device == "auto") {
  const char *resolved = std::getenv("SCHUR_IMU_DEVICE");
  if (!resolved) throw std::runtime_error("Use run.sh with auto, or specify a bound /dev/spidevN.0");
  device = resolved;
 }
 fd_ = open(device.c_str(), O_RDWR | O_CLOEXEC);
 if (fd_ < 0) throw std::runtime_error("Cannot open IMU "+device+": "+strerror(errno));
 try {
  if (flock(fd_, LOCK_EX | LOCK_NB)) throw std::runtime_error("IMU is owned by another process");
  uint8_t mode = SPI_MODE_3, bits = 8; uint32_t speed = 1000000;
  if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) || ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
      ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed)) throw std::runtime_error("IMU SPI configuration failed");
  write(0x7f, 0); expect(0x01, 0x6a); identified_ = true;
  prioritizeSpiWorker();
  initialize();
  std::cerr << "imu: SC7U22 " << device << " SPI=1MHz accel/gyro ODR=200Hz registers=0x89/0x89, FIFO bypass\n";
  worker_ = std::thread(&LiveImu::acquire, this);
 } catch (...) { powerDown(); restoreSpiWorker(); close(fd_); fd_ = -1; throw; }
}
void LiveImu::prioritizeSpiWorker() {
 // The SG2002 has one A53: a realtime caller still blocks behind the normal
 // priority SPI kernel worker unless that worker is also prioritized.
 struct stat device{}; char resolved[PATH_MAX];
 if (fstat(fd_, &device) || !S_ISCHR(device.st_mode)) return;
 const std::string sysfs = "/sys/dev/char/"+std::to_string(major(device.st_rdev))+":"+std::to_string(minor(device.st_rdev));
 if (!realpath(sysfs.c_str(), resolved)) return;
 const std::string physical(resolved), marker = "/41a0000.spi2/spi_master/";
 const auto at = physical.find(marker);
 if (at == std::string::npos) return; // only the TideSense SC7U22 controller
 const auto begin = at+marker.size();
 const std::string wanted = physical.substr(begin,physical.find('/',begin)-begin);
 DIR* tasks = opendir("/proc");
 if (!tasks) return;
 while (auto entry = readdir(tasks)) {
  char* end; const long pid = std::strtol(entry->d_name,&end,10);
  if (!pid || *end) continue;
  const std::string task = "/proc/"+std::to_string(pid);
  std::ifstream comm(task+"/comm"), cmdline(task+"/cmdline");
  std::string name; std::getline(comm,name); char c;
  if (name != wanted || !cmdline || cmdline.get(c) || !cmdline.eof()) continue;
  const int policy = sched_getscheduler(pid);
  sched_param previous{}, priority{}; priority.sched_priority = 11;
  if (policy < 0 || sched_getparam(pid,&previous)) break;
  if ((policy == SCHED_FIFO || policy == SCHED_RR) && previous.sched_priority >= 11) {
   std::cerr << "imu: SPI worker " << wanted << " already realtime, preserving policy\n";
   break;
  }
  if (sched_setscheduler(pid,SCHED_FIFO,&priority)) {
   std::cerr << "imu: cannot prioritize SPI worker: " << strerror(errno) << '\n';
   break;
  }
  spi_worker_=pid; spi_policy_=policy; spi_priority_=previous; spi_worker_name_=wanted;
  std::cerr << "imu: SPI worker " << wanted << " SCHED_FIFO/11 (restored on exit)\n";
  break;
 }
 closedir(tasks);
}
void LiveImu::restoreSpiWorker() noexcept {
 if (spi_worker_ < 0) return;
 // The controller's kernel thread persists for the lifetime of its driver.
 // Check its name again instead of changing an unrelated task after hot-unbind.
 std::ifstream comm("/proc/"+std::to_string(spi_worker_)+"/comm");
 std::string name; std::getline(comm,name);
 if (name == spi_worker_name_ && sched_setscheduler(spi_worker_,spi_policy_,&spi_priority_))
  std::cerr << "imu: SPI worker policy restore failed: " << strerror(errno) << '\n';
 spi_worker_=-1;
}
void LiveImu::initialize() {
 write(0x7f,0); write(0x04,0x50); write(0x4a,0xa5); delay(1);
 write(0x4a,0xa5); delay(200);
 write(0x7f,0); write(0x04,0x50); write(0x7d,0); delay(1);
 // SC7U22TH manual 5.40/5.42: ODR=0b1001=200Hz, performance mode,
 // OSR4 averaging. BDU and address increment make the 12-byte burst coherent.
 write(0x41,3); write(0x43,0); write(0x40,0x89); write(0x42,0x89); delay(2);
 write(0x1c,0); write(0x1d,0); write(0x7d,0x0e); delay(60);
 expect(0x01,0x6a); expect(0x04,0x50); expect(0x41,3); expect(0x43,0);
 expect(0x40,0x89); expect(0x42,0x89); expect(0x1d,0); expect(0x7d,0x0e);
 uint8_t status; read(0x0b,&status,1);
 if (status & 0x30) throw std::runtime_error("IMU reports accel/gyro configuration error");
 uint8_t discard[12]; read(0x0c,discard,sizeof(discard)); // discard data held during settling
}
void LiveImu::acquire() {
 try {
  sched_param priority{}; priority.sched_priority = 10;
  int sched = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);
  std::cerr << "imu: acquisition scheduling=" << (sched ? "normal" : "SCHED_FIFO/10") << '\n';
  uint64_t deadline = monotonicNs(), last_ready = deadline;
  while (!stopping_) {
   deadline += 500000; // poll DRDY at 2kHz; publish only new hardware 200Hz samples
   uint64_t now = monotonicNs();
   if (now > deadline) deadline = now;
   timespec wake{time_t(deadline/1000000000ULL), long(deadline%1000000000ULL)};
   int sleep_rc;
   do { sleep_rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr); }
   while (sleep_rc == EINTR && !stopping_);
   if (sleep_rc && sleep_rc != EINTR) throw std::runtime_error("IMU polling clock failed");
   if (stopping_) break;
   uint8_t status; read(0x0b,&status,1);
   if (status & 0x30) throw std::runtime_error("IMU configuration error while running");
   if ((status & 3) != 3) {
    if (monotonicNs()-last_ready > 100000000) throw std::runtime_error("IMU data-ready stalled for 100ms");
    continue;
   }
   uint8_t raw[12]; uint64_t before = monotonicNs(); read(0x0c,raw,sizeof(raw));
   uint64_t after = monotonicNs();
   ImuSample sample{}; sample.stamp_ns = before+(after-before)/2; last_ready = after;
   for (int axis = 0; axis < 3; ++axis) {
    int16_t a = be16(raw+axis*2), g = be16(raw+6+axis*2);
    if (a >= 32760 || a <= -32760 || g >= 32760 || g <= -32760)
     throw std::runtime_error("IMU raw sample saturated");
    sample.accel[axis] = a*(16.0*9.80665/32768.0);
    sample.gyro[axis] = g*(2000.0*3.14159265358979323846/180.0/32768.0);
   }
   std::lock_guard<std::mutex> lock(mutex_);
   if (last_) {
    uint64_t gap = sample.stamp_ns-last_;
    if (sample.stamp_ns <= last_ || gap >= 15000000)
     throw std::runtime_error("IMU sample timing gap reaches SchurVINS 15ms limit; refusing invalid integration");
    max_gap_ = std::max(max_gap_,gap); if (gap > 7500000) ++late_;
   } else first_ = sample.stamp_ns;
   if (queue_.size() >= 2000) throw std::runtime_error("IMU history full (10s); refusing to discard unconsumed IMU");
   queue_.push_back(sample); last_ = sample.stamp_ns; ++samples_;
   high_water_ = std::max(high_water_,queue_.size()); ready_.notify_one();
  }
 } catch (const std::exception& e) {
  std::lock_guard<std::mutex> lock(mutex_); error_ = e.what(); ready_.notify_all();
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
 if (!identified_ || fd_ < 0) return;
 try { write(0x1d,0); write(0x7d,0); expect(0x7d,0); }
 catch (const std::exception& e) { std::cerr << "imu: power down failed: " << e.what() << '\n'; }
 identified_ = false;
}
void LiveImu::stop() {
 stopping_ = true; ready_.notify_all();
 if (worker_.joinable()) worker_.join();
 powerDown(); restoreSpiWorker();
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

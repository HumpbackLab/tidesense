// SPDX-License-Identifier: GPL-3.0-or-later
#include "micro_ros_bridge.hpp"
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
bool requested() {
  const char* s = std::getenv("SCHUR_MICRO_ROS");
  if (!s || std::string(s) == "0") return false;
  if (std::string(s) == "1") return true;
  throw std::runtime_error("SCHUR_MICRO_ROS must be 0 or 1");
}
}

#ifdef SCHUR_WITH_MICRO_ROS
#include <arpa/inet.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include <deque>
#include <sys/resource.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <geometry_msgs/msg/pose_stamped.h>
#include <std_msgs/msg/bool.h>
#include <sensor_msgs/msg/image.h>
#include <sensor_msgs/msg/imu.h>
#include <rosidl_runtime_c/string_functions.h>
#include <rosidl_runtime_c/primitives_sequence_functions.h>

namespace {
std::string env(const char* name, const char* fallback) {
  const char* s = std::getenv(name);
  return s ? s : fallback;
}
unsigned integer(const char* name, unsigned fallback, unsigned min, unsigned max) {
  const std::string s = env(name, std::to_string(fallback).c_str());
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error(std::string("Invalid ") + name);
  const auto n = std::stoull(s);
  if (n < min || n > max) throw std::runtime_error(std::string("Out of range: ") + name);
  return static_cast<unsigned>(n);
}
void check(rcl_ret_t r, const char* what) {
  if (r != RCL_RET_OK) throw std::runtime_error(what);
}
// Cleanup errors must not abort VIO or mask the original connection failure.
void discard(rcl_ret_t r) { if (r != RCL_RET_OK) rcl_reset_error(); }
}

struct MicroRosBridge::Impl {
  struct Sample {
    uint64_t timestamp = 0;
    bool tracking = false;
    double p[3] = {}, q[4] = {0,0,0,1};
  };
  static constexpr size_t pixels = 640 * 400;
  struct ImuSample { uint64_t timestamp; double gyro[3], accel[3]; };
  const bool calibration, imu_enabled;
  std::deque<ImuSample> imu_queue;
  std::atomic<bool> imu_failed{false};
  uint64_t imu_published = 0;
  size_t imu_high_water = 0;
  static constexpr size_t imu_capacity = 2000; // 10 seconds at 200 Hz.

  std::vector<uint8_t> image_pair;
  std::string agent, port;
  unsigned domain;
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> dropped{0};
  std::mutex mutex;
  std::condition_variable wake;
  Sample latest;
  bool pending = false;
  std::thread worker;
  uint64_t published = 0, connections = 0, image_pairs = 0;

  explicit Impl(bool images, bool imu) : calibration(images), imu_enabled(imu), image_pair(images ? pixels*2 : 0), agent(env("SCHUR_MICRO_ROS_AGENT", "")),
           port(std::to_string(integer("SCHUR_MICRO_ROS_PORT", 8888, 1, 65535))),
           domain(integer("SCHUR_MICRO_ROS_DOMAIN", 0, 0, 232)) {
    if (imu && !images) throw std::runtime_error("IMU publishing requires calibration mode");
    in_addr addr;
    if (inet_pton(AF_INET, agent.c_str(), &addr) != 1)
      throw std::runtime_error("Set SCHUR_MICRO_ROS_AGENT to the computer's numeric IPv4 address");
    worker = std::thread(&Impl::run, this);
  }
  ~Impl() {
    stop = true;
    wake.notify_one();
    worker.join();
    std::cerr << "micro-ROS: connections=" << connections << " poses=" << published
              << " image_pairs=" << image_pairs << " imu_published=" << imu_published
              << " imu_queue_high_water=" << imu_high_water
              << " overwritten_or_busy=" << dropped.load() << '\n';
  }
  void submit(const Sample& s) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) { ++dropped; return; }
    if (pending) ++dropped;
    latest = s;
    pending = true;
    lock.unlock();
    wake.notify_one();
  }
  bool submitImages(uint64_t timestamp, const uint8_t* left, const uint8_t* right) {
    if (!calibration || !left || !right) throw std::runtime_error("Image submission requires calibration mode");
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) { ++dropped; return false; }
    if (pending) ++dropped;
    std::memcpy(image_pair.data(), left, pixels);
    std::memcpy(image_pair.data()+pixels, right, pixels);
    latest.timestamp = timestamp;
    pending = true;
    lock.unlock();
    wake.notify_one();
    return true;
  }
  bool submitImu(uint64_t timestamp, const double* gyro, const double* accel) {
    if (!imu_enabled) throw std::runtime_error("IMU publishing was not enabled");
    std::lock_guard<std::mutex> lock(mutex);
    if (imu_failed || stop || imu_queue.size() >= imu_capacity) return false;
    ImuSample s{}; s.timestamp = timestamp;
    std::memcpy(s.gyro, gyro, sizeof(s.gyro)); std::memcpy(s.accel, accel, sizeof(s.accel));
    imu_queue.push_back(s);
    imu_high_water = std::max(imu_high_water, imu_queue.size());
    wake.notify_one();
    return true;
  }
  void finishImu() {
    if (!imu_enabled) return;
    std::unique_lock<std::mutex> lock(mutex);
    if (!wake.wait_for(lock, std::chrono::seconds(3), [this] { return imu_queue.empty() || imu_failed.load(); }) || imu_failed)
      throw std::runtime_error("IMU publication incomplete; discard incomplete calibration recording");
  }
  void pause() {
    std::unique_lock<std::mutex> lock(mutex);
    wake.wait_for(lock, std::chrono::seconds(1), [this]{return stop.load();});
  }
  void session(rcl_init_options_t& options) {
    rclc_support_t support{};
    rcl_node_t node = rcl_get_zero_initialized_node();
    rcl_publisher_t pose_pub = rcl_get_zero_initialized_publisher();
    rcl_publisher_t tracking_pub = rcl_get_zero_initialized_publisher();
    geometry_msgs__msg__PoseStamped pose{};
    std_msgs__msg__Bool tracking{};
    sensor_msgs__msg__Image images[2]{};
    sensor_msgs__msg__Imu imu{};
    rcl_publisher_t imu_pub = rcl_get_zero_initialized_publisher();
    bool imu_ok = false, imu_message_ok = false;
    bool images_ok[2] = {false, false};
    auto allocator = rcl_get_default_allocator();
    bool support_ok = false, node_ok = false, pose_ok = false, tracking_ok = false;
    bool message_ok = false;
    auto cleanup = [&] {
      if (support_ok) {
        discard(rmw_uros_set_context_entity_destroy_session_timeout(
          rcl_context_get_rmw_context(&support.context), 0));
      }
      if (imu_ok) discard(rcl_publisher_fini(&imu_pub, &node));
      if (imu_message_ok) sensor_msgs__msg__Imu__fini(&imu);
      if (tracking_ok) discard(rcl_publisher_fini(&tracking_pub, &node));
      if (pose_ok) discard(rcl_publisher_fini(&pose_pub, &node));
      if (node_ok) discard(rcl_node_fini(&node));
      if (support_ok) discard(rclc_support_fini(&support));
      if (message_ok) geometry_msgs__msg__PoseStamped__fini(&pose);
      for (int i = 0; i < 2; ++i) if (images_ok[i]) sensor_msgs__msg__Image__fini(&images[i]);
    };
    try {
      const auto init_result = rclc_support_init_with_options(&support, 0, nullptr, &options, &allocator);
      support_ok = rcl_context_is_valid(&support.context);
      check(init_result, "support init");
      check(rmw_uros_set_context_entity_creation_session_timeout(
        rcl_context_get_rmw_context(&support.context), 1000), "creation timeout");
      check(rclc_node_init_default(&node, "tidesense", "/tidesense", &support), "node init");
      node_ok = true;
      if (calibration) {
        rcl_publisher_t* pubs[] = {&pose_pub, &tracking_pub};
        const char* topics[] = {"left/image_raw", "right/image_raw"};
        const char* frames[] = {"left_camera_optical_frame", "right_camera_optical_frame"};
        for (int i = 0; i < 2; ++i) {
          auto qos = rmw_qos_profile_default;
          qos.depth = 1;  // reliable XRCE stream supports large-image fragmentation
          check(rclc_publisher_init(pubs[i], &node,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Image), topics[i], &qos), "image publisher");
          (i == 0 ? pose_ok : tracking_ok) = true;
          check(rmw_uros_set_publisher_session_timeout(rcl_publisher_get_rmw_handle(pubs[i]), 500), "image timeout");
          if (!sensor_msgs__msg__Image__init(&images[i])) throw std::runtime_error("image init");
          images_ok[i] = true;
          if (!rosidl_runtime_c__String__assign(&images[i].header.frame_id, frames[i]) ||
              !rosidl_runtime_c__String__assign(&images[i].encoding, "mono8") ||
              !rosidl_runtime_c__uint8__Sequence__init(&images[i].data, pixels))
            throw std::runtime_error("image allocation");
          images[i].width = images[i].step = 640;
          images[i].height = 400;
          images[i].is_bigendian = false;
        }
      } else {
        check(rclc_publisher_init_best_effort(&pose_pub, &node,
          ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, PoseStamped), "pose"), "pose publisher");
        pose_ok = true;
        check(rclc_publisher_init_best_effort(&tracking_pub, &node,
          ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Bool), "tracking"), "tracking publisher");
        tracking_ok = true;
        if (!geometry_msgs__msg__PoseStamped__init(&pose)) throw std::runtime_error("pose allocation");
        message_ok = true;
        if (!rosidl_runtime_c__String__assign(&pose.header.frame_id, "odom"))
          throw std::runtime_error("frame_id allocation");
      }
      if (imu_enabled) {
        auto qos = rmw_qos_profile_default; qos.depth = 200;
        check(rclc_publisher_init(&imu_pub, &node,
          ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Imu), "imu", &qos), "IMU publisher");
        imu_ok = true;
        check(rmw_uros_set_publisher_session_timeout(rcl_publisher_get_rmw_handle(&imu_pub), 100), "IMU timeout");
        if (!sensor_msgs__msg__Imu__init(&imu)) throw std::runtime_error("IMU message init");
        imu_message_ok = true;
        if (!rosidl_runtime_c__String__assign(&imu.header.frame_id, "imu_link"))
          throw std::runtime_error("IMU frame allocation");
        imu.orientation.w = 1;
        imu.orientation_covariance[0] = -1; // Orientation unavailable; no fusion here.
        // Zero angular/linear covariance means unknown, not zero sensor noise.
      }
      ++connections;
      std::cerr << "micro-ROS: connected " << agent << ':' << port << " domain=" << domain << '\n';
      auto next_ping = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      while (!stop) {
        Sample s;
        bool have_sample;
        {
          std::unique_lock<std::mutex> lock(mutex);
          wake.wait_until(lock, next_ping, [this]{return pending || !imu_queue.empty() || stop.load();});
          if (stop) break;
          have_sample = pending;
          if (pending) {
            s = latest; pending = false;
            if (calibration) {
              std::memcpy(images[0].data.data, image_pair.data(), pixels);
              std::memcpy(images[1].data.data, image_pair.data()+pixels, pixels);
            }
          }
        }
        if (std::chrono::steady_clock::now() >= next_ping) {
          if (rmw_uros_ping_agent(100, 1) != RMW_RET_OK) throw std::runtime_error("agent disconnected");
          next_ping = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        // Publish a bounded batch before images. Keep each FIFO entry until
        // successful publication; an error ends combined calibration explicitly.
        for (unsigned n = 0; imu_enabled && n < 64 && !stop; ++n) {
          ImuSample sample;
          { std::lock_guard<std::mutex> lock(mutex);
            if (imu_queue.empty()) break;
            sample = imu_queue.front();
          }
          if (sample.timestamp / 1000000000ULL > uint64_t(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("IMU timestamp exceeds ROS range");
          imu.header.stamp.sec = int32_t(sample.timestamp / 1000000000ULL);
          imu.header.stamp.nanosec = uint32_t(sample.timestamp % 1000000000ULL);
          imu.angular_velocity.x = sample.gyro[0]; imu.angular_velocity.y = sample.gyro[1]; imu.angular_velocity.z = sample.gyro[2];
          imu.linear_acceleration.x = sample.accel[0]; imu.linear_acceleration.y = sample.accel[1]; imu.linear_acceleration.z = sample.accel[2];
          check(rcl_publish(&imu_pub, &imu, nullptr), "IMU publish");
          { std::lock_guard<std::mutex> lock(mutex); imu_queue.pop_front(); ++imu_published; }
          wake.notify_all();
        }
        if (!have_sample) continue;
        if (calibration) {
          if (s.timestamp / 1000000000ULL > uint64_t(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("image timestamp exceeds ROS time range");
          for (auto& im : images) {
            im.header.stamp.sec = static_cast<int32_t>(s.timestamp / 1000000000ULL);
            im.header.stamp.nanosec = static_cast<uint32_t>(s.timestamp % 1000000000ULL);
          }
          check(rcl_publish(&pose_pub, &images[0], nullptr), "left image publish");
          if (stop) break;
          check(rcl_publish(&tracking_pub, &images[1], nullptr), "right image publish");
          ++image_pairs;
          continue;
        }
        tracking.data = s.tracking;
        check(rcl_publish(&tracking_pub, &tracking, nullptr), "tracking publish");
        if (!s.tracking) continue;
        // ROS builtin time uses signed 32-bit seconds.
        if (s.timestamp / 1000000000ULL > uint64_t(std::numeric_limits<int32_t>::max())) continue;
        pose.header.stamp.sec = static_cast<int32_t>(s.timestamp / 1000000000ULL);
        pose.header.stamp.nanosec = static_cast<uint32_t>(s.timestamp % 1000000000ULL);
        pose.pose.position.x = s.p[0]; pose.pose.position.y = s.p[1]; pose.pose.position.z = s.p[2];
        pose.pose.orientation.x = s.q[0]; pose.pose.orientation.y = s.q[1];
        pose.pose.orientation.z = s.q[2]; pose.pose.orientation.w = s.q[3];
        check(rcl_publish(&pose_pub, &pose, nullptr), "pose publish");
        ++published;
      }
    } catch (...) { cleanup(); throw; }
    cleanup();
  }
  void run() noexcept {
    // Linux nice is per-thread: reduce contention with VIO on the single A53.
    setpriority(PRIO_PROCESS, 0, 5);
    rcl_init_options_t options = rcl_get_zero_initialized_init_options();
    bool initialized = false;
    try {
      check(rcl_init_options_init(&options, rcl_get_default_allocator()), "init options");
      initialized = true;
      check(rcl_init_options_set_domain_id(&options, domain), "domain");
      auto* rmw_options = rcl_init_options_get_rmw_init_options(&options);
      check(rmw_uros_options_set_udp_address(agent.c_str(), port.c_str(), rmw_options), "UDP address");
      std::cerr << "micro-ROS: waiting for " << agent << ':' << port << " (VIO remains independent)\n";
      while (!stop) {
        if (rmw_uros_ping_agent_options(100, 1, rmw_options) == RMW_RET_OK) {
          try { session(options); }
          catch (const std::exception& e) {
            std::cerr << "micro-ROS: " << e.what() << (imu_enabled ? "; calibration stopped\n" : "; retrying\n");
            rcl_reset_error();
            if (imu_enabled) { imu_failed = true; stop = true; wake.notify_all(); }
          }
        }
        if (!stop) pause();
      }
    } catch (const std::exception& e) {
      imu_failed = imu_enabled; wake.notify_all();
      std::cerr << "micro-ROS: communication stopped: " << e.what() << '\n';
      rcl_reset_error();
    }
    if (initialized) discard(rcl_init_options_fini(&options));
  }
};
MicroRosBridge::MicroRosBridge(bool calibration, bool imu) { if (requested()) impl_.reset(new Impl(calibration, imu)); }
bool MicroRosBridge::submitImu(uint64_t t, const double* g, const double* a) { return impl_ && impl_->submitImu(t,g,a); }
void MicroRosBridge::finishImu() { if (impl_) impl_->finishImu(); }
bool MicroRosBridge::submitImages(uint64_t t, const uint8_t* l, const uint8_t* r) {
  return impl_ && impl_->submitImages(t, l, r);
}
bool MicroRosBridge::compiled() { return true; }
void MicroRosBridge::submit(uint64_t t, bool tracking, const double* p, const double* q) {
  if (!impl_) return;
  Impl::Sample s;
  s.timestamp = t; s.tracking = tracking && p && q;
  if (s.tracking) { std::memcpy(s.p, p, sizeof(s.p)); std::memcpy(s.q, q, sizeof(s.q)); }
  impl_->submit(s);
}
#else
struct MicroRosBridge::Impl {};
MicroRosBridge::MicroRosBridge(bool, bool) {
  if (requested()) throw std::runtime_error("micro-ROS was not compiled in; rebuild with SCHUR_WITH_MICRO_ROS=ON or set SCHUR_MICRO_ROS=0");
}
bool MicroRosBridge::compiled() { return false; }
bool MicroRosBridge::submitImu(uint64_t, const double*, const double*) { return false; }
void MicroRosBridge::finishImu() {}
void MicroRosBridge::submit(uint64_t, bool, const double*, const double*) {}
bool MicroRosBridge::submitImages(uint64_t, const uint8_t*, const uint8_t*) { return false; }
#endif
MicroRosBridge::~MicroRosBridge() = default;
bool MicroRosBridge::enabled() const { return bool(impl_); }

// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth_ros.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <sensor_msgs/msg/image.h>
#include <sensor_msgs/msg/camera_info.h>
#include <rosidl_runtime_c/string_functions.h>
#include <rosidl_runtime_c/primitives_sequence_functions.h>

namespace {
std::string env(const char* key, const char* fallback) {
  auto s = std::getenv(key); return s ? s : fallback;
}
unsigned number(const char* key, unsigned fallback, unsigned min, unsigned max) {
  auto s = env(key,std::to_string(fallback).c_str());
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error(std::string("Invalid ")+key);
  auto n = std::stoull(s);
  if (n < min || n > max) throw std::runtime_error(std::string("Out of range: ")+key);
  return unsigned(n);
}
void check(rcl_ret_t result, const char* what) {
  if (result != RCL_RET_OK) throw std::runtime_error(what);
}
void discard(rcl_ret_t result) { if (result != RCL_RET_OK) rcl_reset_error(); }
}

struct DepthRos::Impl {
  const DepthIntrinsics info;
  const std::string agent, port;
  const unsigned domain;
  std::vector<uint8_t> latest;
  uint64_t stamp = 0, published = 0, connections = 0;
  std::atomic<uint64_t> dropped{0};
  std::atomic<bool> stop{false};
  bool pending = false;
  std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;

  explicit Impl(const DepthIntrinsics& i) : info(i), agent(env("SCHUR_MICRO_ROS_AGENT","")),
      port(std::to_string(number("SCHUR_MICRO_ROS_PORT",8888,1,65535))),
      domain(number("SCHUR_MICRO_ROS_DOMAIN",0,0,232)), latest(size_t(i.width)*i.height*2) {
    in_addr addr;
    if (inet_pton(AF_INET,agent.c_str(),&addr) != 1)
      throw std::runtime_error("Set SCHUR_MICRO_ROS_AGENT to the computer's numeric IPv4 address");
    if (env("SCHUR_MICRO_ROS","1") != "1")
      throw std::runtime_error("Depth mode requires SCHUR_MICRO_ROS=1");
    worker = std::thread(&Impl::run,this);
  }
  ~Impl() {
    stop = true; wake.notify_all(); worker.join();
    std::cerr << "depth ROS: connections=" << connections << " published=" << published
              << " overwritten_or_busy=" << dropped.load() << '\n';
  }
  bool submit(uint64_t t, const cv::Mat& depth) {
    if (t/1000000000ULL > INT32_MAX || depth.type() != CV_16UC1 ||
        depth.cols != info.width || depth.rows != info.height || !depth.isContinuous())
      throw std::runtime_error("Invalid ROS depth image or timestamp");
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
    if (!lock.owns_lock()) { ++dropped; return false; }
    if (pending) ++dropped;
    std::memcpy(latest.data(),depth.data,latest.size());
    stamp = t; pending = true;
    lock.unlock(); wake.notify_one(); return true;
  }
  void session(rcl_init_options_t& options) {
    rclc_support_t support{};
    auto node = rcl_get_zero_initialized_node();
    auto image_pub = rcl_get_zero_initialized_publisher();
    auto info_pub = rcl_get_zero_initialized_publisher();
    sensor_msgs__msg__Image image{};
    sensor_msgs__msg__CameraInfo camera{};
    bool support_ok=false, node_ok=false, image_pub_ok=false, info_pub_ok=false;
    bool image_ok=false, camera_ok=false;
    auto cleanup = [&] {
      if (support_ok) discard(rmw_uros_set_context_entity_destroy_session_timeout(
        rcl_context_get_rmw_context(&support.context),0));
      if (info_pub_ok) discard(rcl_publisher_fini(&info_pub,&node));
      if (image_pub_ok) discard(rcl_publisher_fini(&image_pub,&node));
      if (node_ok) discard(rcl_node_fini(&node));
      if (support_ok) discard(rclc_support_fini(&support));
      if (camera_ok) sensor_msgs__msg__CameraInfo__fini(&camera);
      if (image_ok) sensor_msgs__msg__Image__fini(&image);
    };
    try {
      auto allocator = rcl_get_default_allocator();
      auto result = rclc_support_init_with_options(&support,0,nullptr,&options,&allocator);
      support_ok = rcl_context_is_valid(&support.context);
      check(result,"support init");
      check(rmw_uros_set_context_entity_creation_session_timeout(
        rcl_context_get_rmw_context(&support.context),1000),"entity timeout");
      check(rclc_node_init_default(&node,"tidesense_depth","/tidesense",&support),"node init");
      node_ok = true;
      // Reliable XRCE streams fragment images larger than the 1400-byte UDP MTU.
      auto qos = rmw_qos_profile_default; qos.depth = 1;
      check(rclc_publisher_init(&image_pub,&node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs,msg,Image),"depth/image_raw",&qos),"image publisher");
      image_pub_ok = true;
      check(rclc_publisher_init(&info_pub,&node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs,msg,CameraInfo),"depth/camera_info",&qos),"CameraInfo publisher");
      info_pub_ok = true;
      check(rmw_uros_set_publisher_session_timeout(rcl_publisher_get_rmw_handle(&image_pub),500),"image timeout");
      check(rmw_uros_set_publisher_session_timeout(rcl_publisher_get_rmw_handle(&info_pub),100),"CameraInfo timeout");
      if (!sensor_msgs__msg__Image__init(&image)) throw std::runtime_error("image init");
      image_ok = true;
      if (!sensor_msgs__msg__CameraInfo__init(&camera)) throw std::runtime_error("CameraInfo init");
      camera_ok = true;
      if (!rosidl_runtime_c__String__assign(&image.header.frame_id,"depth_optical_frame") ||
          !rosidl_runtime_c__String__assign(&camera.header.frame_id,"depth_optical_frame") ||
          !rosidl_runtime_c__String__assign(&image.encoding,"16UC1") ||
          !rosidl_runtime_c__String__assign(&camera.distortion_model,"plumb_bob") ||
          !rosidl_runtime_c__uint8__Sequence__init(&image.data,latest.size()) ||
          !rosidl_runtime_c__double__Sequence__init(&camera.d,5))
        throw std::runtime_error("depth ROS allocation");
      image.width = camera.width = info.width;
      image.height = camera.height = info.height;
      image.step = info.width*2; image.is_bigendian = false;
      std::memset(camera.d.data,0,camera.d.size*sizeof(double));
      // CameraInfo describes the virtual rectified depth camera, not a raw sensor.
      camera.k[0]=camera.p[0]=info.fx; camera.k[4]=camera.p[5]=info.fy;
      camera.k[2]=camera.p[2]=info.cx; camera.k[5]=camera.p[6]=info.cy;
      camera.k[8]=camera.p[10]=1;
      camera.r[0]=camera.r[4]=camera.r[8]=1;
      ++connections;
      // Never replay a queued frame from before a connection/reconnection.
      { std::lock_guard<std::mutex> lock(mutex); if (pending) ++dropped; pending=false; }
      std::cerr << "depth ROS: connected " << agent << ':' << port << " domain=" << domain << '\n';
      auto ping = std::chrono::steady_clock::now()+std::chrono::seconds(1);
      while (!stop) {
        uint64_t t=0;
        {
          std::unique_lock<std::mutex> lock(mutex);
          wake.wait_until(lock,ping,[this]{ return pending || stop.load(); });
          if (stop) break;
          if (pending) { t=stamp; std::memcpy(image.data.data,latest.data(),latest.size()); pending=false; }
        }
        if (std::chrono::steady_clock::now() >= ping) {
          if (rmw_uros_ping_agent(100,1) != RMW_RET_OK) throw std::runtime_error("agent disconnected");
          ping = std::chrono::steady_clock::now()+std::chrono::seconds(1);
        }
        if (!t) continue;
        image.header.stamp.sec = int32_t(t/1000000000ULL);
        image.header.stamp.nanosec = uint32_t(t%1000000000ULL);
        camera.header.stamp = image.header.stamp;
        check(rcl_publish(&info_pub,&camera,nullptr),"CameraInfo publish");
        check(rcl_publish(&image_pub,&image,nullptr),"depth publish");
        ++published;
      }
    } catch (...) { cleanup(); throw; }
    cleanup();
  }
  void run() noexcept {
    setpriority(PRIO_PROCESS,0,5);
    auto options = rcl_get_zero_initialized_init_options();
    bool initialized=false;
    try {
      check(rcl_init_options_init(&options,rcl_get_default_allocator()),"init options");
      initialized=true;
      check(rcl_init_options_set_domain_id(&options,domain),"domain");
      auto rmw = rcl_init_options_get_rmw_init_options(&options);
      check(rmw_uros_options_set_udp_address(agent.c_str(),port.c_str(),rmw),"UDP address");
      while (!stop) {
        if (rmw_uros_ping_agent_options(100,1,rmw) == RMW_RET_OK) {
          try { session(options); }
          catch (const std::exception& e) { std::cerr << "depth ROS: " << e.what() << "; retrying\n"; rcl_reset_error(); }
        }
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock,std::chrono::seconds(1),[this]{ return stop.load(); });
      }
    } catch (const std::exception& e) {
      std::cerr << "depth ROS: communication stopped: " << e.what() << '\n'; rcl_reset_error();
    }
    if (initialized) discard(rcl_init_options_fini(&options));
  }
};
DepthRos::DepthRos(const DepthIntrinsics& info) : impl_(new Impl(info)) {}
DepthRos::~DepthRos() = default;
bool DepthRos::submit(uint64_t t, const cv::Mat& depth) { return impl_->submit(t,depth); }

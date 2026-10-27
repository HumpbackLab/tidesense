// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth.hpp"
#include "depth_ros.hpp"
#include "tide_camera.hpp"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <time.h>

namespace {
volatile sig_atomic_t stopped = 0;
void stop(int) { stopped=1; }
uint64_t now(clockid_t clock) {
  timespec t{};
  if (clock_gettime(clock,&t)) throw std::runtime_error("clock_gettime failed");
  return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
uint64_t integer(const std::string& s) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("Expected a nonnegative integer");
  return std::stoull(s);
}
}
int main(int argc, char** argv) try {
  const auto bin = executableDirectory();
  std::string calibration=bin+"/../calibration.yaml";
  int width=320, disparities=0;
  double fps=20;
  uint64_t frames=0;
  for (int i=1; i<argc; ++i) {
    std::string option=argv[i];
    if (option == "--help") {
      std::cout << "tidesense_depth [--calibration SCHUR.yaml]\n"
        "  [--width 320|640] [--disparities 16..WIDTH-16] [--fps 0.1..20] [--frames N]\n"
        "Default: 320x200, D32, block15, 20Hz selection from FSIN 20Hz.\n"
        "micro-ROS: SCHUR_MICRO_ROS_AGENT=HOST_IP, optional SCHUR_MICRO_ROS_PORT/DOMAIN.\n"
        "Topics: /tidesense/depth/image_raw (16UC1 mm, 0 invalid), depth/camera_info.\n";
      return 0;
    }
    if (++i == argc) throw std::runtime_error("Missing option value");
    std::string value=argv[i];
    if (option == "--calibration") calibration=value;
    else if (option == "--frames") frames=integer(value);
    else if (option == "--width" || option == "--disparities") {
      auto n=integer(value);
      if (n>640) throw std::runtime_error("Width/disparities exceeds 640");
      (option == "--width" ? width : disparities)=int(n);
    } else if (option == "--fps") {
      size_t end;
      fps=std::stod(value,&end);
      if (end != value.size() || !std::isfinite(fps) || fps<0.1 || fps>20)
        throw std::runtime_error("fps must be in [0.1,20]");
    } else throw std::runtime_error("Unknown option: "+option);
  }
  cv::setNumThreads(1); cv::setUseOptimized(true);
  StereoDepth engine(calibration,width,disparities ? disparities : (width==320 ? 32 : 64));
  const auto& info=engine.intrinsics();
  DepthRos ros(info); // Validate ROS settings before opening hardware.
  std::ofstream profile;
  if (const char* path=std::getenv("TIDESENSE_DEPTH_PROFILE")) {
    profile.exceptions(std::ios::badbit | std::ios::failbit);
    profile.open(path);
    profile << "frame,sequence,stamp_ns,begin_ns,rectify_end_ns,bm_end_ns,end_ns,"
               "begin_cpu_ns,rectify_end_cpu_ns,bm_end_cpu_ns,end_cpu_ns\n";
  }
  struct sigaction sa{}; sa.sa_handler=stop;
  sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
  // Preserve acquisition time, mapped once into board realtime for ROS headers.
  const int64_t offset=int64_t(now(CLOCK_REALTIME))-int64_t(now(CLOCK_MONOTONIC));
  tide_cam_config camera_config;
  tide_cam_config_init(&camera_config);
  camera_config.mode={640,400,TIDE_CAM_MONO8};
  loadCameraSettings(camera_config);
  TideCamera camera(camera_config);
  const uint64_t period=uint64_t(std::llround(1e9/fps));
  uint64_t next=0, processed=0, queued=0, first=0, last=0;
  double compute_ms=0;
  std::cout << "Depth " << info.width << 'x' << info.height << " requested_hz=" << fps
            << " reference=" << (info.reference ? "physical_right" : "physical_left")
            << " fx=" << info.fx << " baseline_m=" << info.baseline << " realtime_offset_ns=" << offset << std::endl;
  while (!stopped && (!frames || processed<frames)) {
    LivePair pair;
    if (!camera.next(pair,stopped)) break;
    const auto& h=pair.header;
    if (!(h.flags & CAMERA_FSIN)) throw std::runtime_error("Depth requires hw_sync=1 on both cameras");
    const uint64_t skew=h.left_ns>h.right_ns ? h.left_ns-h.right_ns : h.right_ns-h.left_ns;
    if (skew>10000000) throw std::runtime_error("Stereo VI timestamp skew exceeds 10ms");
    if (!first) std::cout << "camera: first pair sequence=" << h.sequence << std::endl;
    const uint64_t stamp=info.reference ? h.right_ns : h.left_ns;
    if (stamp<next) continue;
    if (!next) next=stamp;
    next+=((stamp-next)/period+1)*period;
    auto start=std::chrono::steady_clock::now();
    DepthTiming timing;
    auto depth=engine.compute(pair.left,pair.right,profile.is_open() ? &timing : nullptr);
    compute_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if (stamp>uint64_t(INT64_MAX) || (offset>0 && stamp>uint64_t(INT64_MAX-offset)) || int64_t(stamp)+offset<0)
      throw std::runtime_error("Acquisition timestamp outside ROS time range");
    queued+=ros.submit(uint64_t(int64_t(stamp)+offset),depth);
    if (!processed) first=stamp;
    last=stamp; ++processed;
    if (profile.is_open()) {
      profile << processed << ',' << h.sequence << ',' << stamp;
      for (auto t : timing.wall) profile << ',' << t;
      for (auto t : timing.cpu) profile << ',' << t;
      profile << '\n';
    }
    if (processed%100==0) std::cout << "depth processed=" << processed << " mean_compute_ms=" << compute_ms/processed << std::endl;
  }
  auto status=camera.stop(); camera.report();
  if (profile.is_open()) profile.close();
  std::cout << "Depth processed=" << processed << " queued=" << queued
            << " mean_compute_ms=" << (processed ? compute_ms/processed : 0)
            << " selected_hz=" << (last>first ? (processed-1)*1e9/(last-first) : 0) << std::endl;
  if (status) throw std::runtime_error("tide_cam failed during cleanup");
  return 0;
} catch (const std::exception& e) { std::cerr << "Depth error: " << e.what() << '\n'; return 1; }

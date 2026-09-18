// SPDX-License-Identifier: GPL-3.0-or-later
#include "calibration.hpp"
#include "live_camera.hpp"
#include "micro_ros_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <time.h>

namespace {
volatile sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
uint64_t now(clockid_t id) {
  timespec t;
  if (clock_gettime(id, &t)) throw std::runtime_error("clock_gettime failed");
  return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
uint64_t count(const std::string& s) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("--frames must be a nonnegative integer");
  return std::stoull(s);
}
}

int runCalibration(int argc, char** argv) {
  try {
    if (argc < 5) throw std::runtime_error("Usage: --calibrate SENSOR.ini --fps HZ [--frames N] [--timestamps CSV]");
    double fps = 0;
    uint64_t limit = 0;
    bool got_fps = false, got_frames = false, got_csv = false;
    std::string csv;
    for (int i = 3; i < argc; i += 2) {
      if (i+1 == argc) throw std::runtime_error("Missing calibration option value");
      const std::string opt = argv[i], value = argv[i+1];
      if (opt == "--fps" && !got_fps) {
        size_t end;
        fps = std::stod(value, &end);
        if (end != value.size() || !std::isfinite(fps) || fps < 0.1 || fps > 20)
          throw std::runtime_error("--fps must be in [0.1, 20] Hz (camera acquisition is 20 Hz)");
        got_fps = true;
      } else if (opt == "--frames" && !got_frames) {
        limit = count(value); got_frames = true;
      } else if (opt == "--timestamps" && !got_csv) {
        csv = value; got_csv = true;
      } else throw std::runtime_error("Unknown or duplicate calibration option: " + opt);
    }
    if (!got_fps) throw std::runtime_error("--fps is required");
    if (!MicroRosBridge::compiled()) throw std::runtime_error("Calibration requires SCHUR_WITH_MICRO_ROS=ON build");
    // Validate communication settings before starting hardware. The existing
    // runtime OFF switch also applies to calibration; never silently override it.
    MicroRosBridge ros(true);
    if (!ros.enabled()) throw std::runtime_error("Calibration requires SCHUR_MICRO_ROS=1 and SCHUR_MICRO_ROS_AGENT");
    std::ofstream stamps;
    if (got_csv) {
      stamps.open(csv);
      if (!stamps) throw std::runtime_error("Cannot open timestamp CSV");
      stamps << "#sequence,left_vi_ns,right_vi_ns,ros_pair_ns,queued\n";
    }
    stopped = 0;
    struct sigaction sa{}; sa.sa_handler = stop;
    sigaction(SIGINT, &sa, nullptr); sigaction(SIGTERM, &sa, nullptr);
    const int64_t offset = int64_t(now(CLOCK_REALTIME))-int64_t(now(CLOCK_MONOTONIC));
    const char* helper = std::getenv("SCHUR_CAMERA_HELPER");
    LiveCamera camera(helper ? helper : executableDirectory()+"/schurvins_camera", argv[2]);
    const uint64_t period = uint64_t(std::llround(1e9/fps));
    uint64_t next = 0, selected = 0, queued = 0, max_skew = 0;
    std::cout << "Calibration: stereo mono8 640x400, requested " << fps
              << " pairs/s; no VIO or IMU. Require FSIN.\n"
              << "Both Image stamps use left VI arrival time mapped to system realtime.\n"
              << "Clock offset_ns=" << offset << "; synchronize board clock before recording.\n";
    while (!stopped && (!limit || selected < limit)) {
      LivePair pair;
      if (!camera.next(pair, stopped)) break;
      const auto& h = pair.header;
      if (!(h.flags & CAMERA_FSIN)) throw std::runtime_error("Calibration requires hw_sync=1 on both cameras");
      if (h.left_ns < next) continue;
      if (!next) next = h.left_ns;
      // Advance the acquisition-time schedule without bursts after stalls.
      next += ((h.left_ns-next)/period+1)*period;
      const auto skew = h.left_ns > h.right_ns ? h.left_ns-h.right_ns : h.right_ns-h.left_ns;
      max_skew = std::max(max_skew, skew);
      const int64_t ros_stamp = int64_t(h.left_ns)+offset;
      if (ros_stamp < 0 || uint64_t(ros_stamp)/1000000000ULL > 2147483647ULL)
        throw std::runtime_error("Board clock outside ROS time range");
      if (!pair.left.isContinuous() || !pair.right.isContinuous())
        throw std::runtime_error("Expected continuous mono8 frames");
      const bool accepted = ros.submitImages(uint64_t(ros_stamp), pair.left.data, pair.right.data);
      ++selected; queued += accepted;
      if (stamps) stamps << h.sequence << ',' << h.left_ns << ',' << h.right_ns << ',' << ros_stamp << ',' << accepted << '\n';
    }
    const int status = camera.stop();
    camera.report();
    std::cout << "Calibration selected_pairs=" << selected << " queued=" << queued
              << " max_vi_skew_ms=" << max_skew/1e6 << '\n';
    if (stamps) { stamps.flush(); if (!stamps) throw std::runtime_error("Timestamp CSV write failed"); }
    // A finite run counts selected input pairs, not guaranteed ROS deliveries.
    if (status != 0 && status != 128+SIGTERM) throw std::runtime_error("Camera helper exited unsuccessfully");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Calibration error: " << e.what() << '\n';
    return 1;
  }
}

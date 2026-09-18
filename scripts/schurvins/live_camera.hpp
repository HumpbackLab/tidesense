#pragma once
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <opencv2/core.hpp>
#include "camera_protocol.h"
struct LivePair {
 CameraHeader header{};
 cv::Mat left, right;
};
// Receiver drains IPC continuously. Exactly one unprocessed pair is retained.
class LiveCamera {
 public:
  LiveCamera(const std::string& helper, const std::string& ini);
  ~LiveCamera();
  LiveCamera(const LiveCamera&) = delete;
  LiveCamera& operator=(const LiveCamera&) = delete;
  bool next(LivePair& pair, const volatile sig_atomic_t& interrupted);
  int stop();
  void report();
 private:
  void receive();
  int fd_ = -1, child_ = -1, stop_status_ = 0;
  std::thread receiver_;
  std::mutex mutex_;
  std::condition_variable ready_;
  LivePair latest_;
  bool pending_ = false, stopping_ = false;
  std::string error_;
  uint64_t received_ = 0, replaced_ = 0;
};
std::string executableDirectory();

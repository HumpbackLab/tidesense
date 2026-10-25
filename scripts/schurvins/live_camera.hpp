#pragma once
#include <condition_variable>
#include <deque>
#include <csignal>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include "camera_frame.hpp"
// Default: latest pair only. A nonzero fifo_capacity selects lossless FIFO;
// overflow is an error, never a silent replacement (for dataset recording).
class LiveCamera {
 public:
  LiveCamera(const std::string& helper, const std::string& ini, size_t fifo_capacity = 0);
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
  const size_t fifo_capacity_;
  std::deque<LivePair> fifo_;
  LivePair latest_;
  bool pending_ = false, stopping_ = false;
  std::string error_;
  uint64_t received_ = 0, replaced_ = 0;
};

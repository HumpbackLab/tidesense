#pragma once
#include <cstdint>
#include <memory>
class TideCamera;

// Application-owned thread: calls the camera library's encoder, then publishes
// borrowed JPEG bytes synchronously. No extra image/JPEG buffers or rate limiter.
class JpegPublisher {
 public:
  JpegPublisher(TideCamera& camera,int64_t ros_clock_offset);
  ~JpegPublisher();
  void finish(); // Camera must be stopped first; reports worker errors.
  JpegPublisher(const JpegPublisher&)=delete;
  JpegPublisher& operator=(const JpegPublisher&)=delete;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

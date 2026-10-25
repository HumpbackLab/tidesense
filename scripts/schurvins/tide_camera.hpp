#pragma once
#include "camera_frame.hpp"
#include <csignal>
#include <tide_cam.h>
#include <yaml-cpp/yaml.h>

// Own image copies: VIO retains keyframes and writers queue frames beyond next().
class TideCamera {
 public:
  explicit TideCamera(const tide_cam_config& config);
  ~TideCamera();
  TideCamera(const TideCamera&)=delete;
  TideCamera& operator=(const TideCamera&)=delete;
  bool next(LivePair& pair,const volatile sig_atomic_t& interrupted);
  int stop();
  void report() const;
 private:
  std::string error(int status) const;
  tide_cam* camera_=nullptr;
};
YAML::Node cameraConfiguration(const tide_cam_config& config);

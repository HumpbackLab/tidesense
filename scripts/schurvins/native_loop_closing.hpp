// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <svo/common/types.h>
#include <svo/common/camera.h>
#include <vikit/cameras/ncamera.h>
#include <yaml-cpp/yaml.h>

// Detection only: immutable keyframe snapshots, one worker, one pending input.
// Results do not change the Schur EKF or its odom / prediction coordinates.
class NativeLoopClosing {
 public:
  NativeLoopClosing(const svo::CameraBundle::Ptr& cameras, const YAML::Node& config);
  ~NativeLoopClosing();
  void submit(const svo::FramePtr& frame);
  void check();
  void finish();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

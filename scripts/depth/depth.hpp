// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <opencv2/calib3d.hpp>
#include <cstdint>
#include <string>

// A virtual, already rectified optical camera. Depth is axial Z, in mm.
struct DepthIntrinsics {
  int width = 0, height = 0, reference = 1;
  double fx = 0, fy = 0, cx = 0, cy = 0, baseline = 0;
};

// Optional profiling only: monotonic stage boundaries and calling-thread CPU time.
struct DepthTiming {
  uint64_t wall[4] = {}, cpu[4] = {};
};

class StereoDepth {
 public:
  StereoDepth(const std::string& calibration, int width, int disparities);
  const DepthIntrinsics& intrinsics() const { return info_; }
  cv::Mat compute(const cv::Mat& left, const cv::Mat& right, DepthTiming* timing = nullptr);
  // Exposed for metric-depth and invalid-pixel verification.
  static cv::Mat millimetres(const cv::Mat& disparity, double fx_baseline,
                            const cv::Mat& reference_valid, const cv::Mat& target_valid);
 private:
  DepthIntrinsics info_;
  cv::Mat map1_[2], map2_[2], valid_[2], rect_[2], disparity_;
  cv::Ptr<cv::StereoBM> bm_;
};

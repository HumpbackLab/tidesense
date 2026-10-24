// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "depth.hpp"
#include "tpu_sad.hpp"
struct PipelineTiming { double downsample=0,rectify=0,prefilter=0,match=0,filter=0,cpu=0; };
class DepthPipeline {
 public:
  explicit DepthPipeline(const std::string& calibration);
  const DepthIntrinsics& intrinsics() const { return geometry_.intrinsics(); }
  cv::Mat compute(const cv::Mat& left,const cv::Mat& right,PipelineTiming& timing);
  std::array<cv::Mat,2> prepare(const cv::Mat& left,const cv::Mat& right,PipelineTiming& timing);
  SadResult match(const std::array<cv::Mat,2>& filtered,PipelineTiming& timing);
  cv::Mat finish(const SadResult& result,const cv::Mat& filtered,PipelineTiming& timing);
  // Q4 disparity; -16 invalid. Conservative endpoints, uniqueness, texture,
  // reverse-warp consistency, subpixel interpolation and speckle rejection.
  static cv::Mat quality(const SadResult& result,const cv::Mat& filtered,int disparities);
 private:
  StereoDepth geometry_;
  TpuStereoDownsample downsample_;
  TpuSad match_;
};

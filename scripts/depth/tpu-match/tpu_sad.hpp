// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <opencv2/core.hpp>
#include <memory>
#include <array>

// Experimental integer SAD + winner-takes-all kernel, not a full StereoBM replacement.
// Inputs: prefiltered mono8 in [0,62]. Valid output: x >= D-1+7, y >= 7,
// x < width-7, y < height-7. Lowest disparity wins ties, zero also a valid winner.
struct SadResult {
  cv::Mat cost, disparity;
  // Optional exact costs: d-1, d+1, minimum over |candidate-d| > 1.
  cv::Mat minus, plus, competitor;
};
struct SadTiming { double upload_ms=0, execute_ms=0, download_ms=0; int launches=0; };
class TpuSad {
 public:
  TpuSad(int width,int height,int disparities,int tile_width=64,int tile_height=10,bool tree=true,bool quality=false);
  ~TpuSad();
  SadResult run(const cv::Mat& reference,const cv::Mat& target,SadTiming& timing);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Exact unsigned 2x2 area reduction, (sum+2)/4. Original images are immutable.
class TpuStereoDownsample {
 public:
  TpuStereoDownsample();
  ~TpuStereoDownsample();
  std::array<cv::Mat,2> run(const cv::Mat& left,const cv::Mat& right,SadTiming& timing);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

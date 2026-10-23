// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <opencv2/core.hpp>
#include <memory>

// Experimental integer SAD + winner-takes-all kernel, not a full StereoBM replacement.
// Inputs: prefiltered mono8 in [0,62]. Valid output: x >= D-1+7, y >= 7,
// x < width-7, y < height-7. Lowest disparity wins ties, zero also a valid winner.
struct SadResult { cv::Mat cost, disparity; };
struct SadTiming { double upload_ms=0, execute_ms=0, download_ms=0; int launches=0; };
class TpuSad {
 public:
  TpuSad(int width,int height,int disparities,int tile_width=64,int tile_height=10,bool tree=true);
  ~TpuSad();
  SadResult run(const cv::Mat& reference,const cv::Mat& target,SadTiming& timing);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

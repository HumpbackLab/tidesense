#pragma once
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <cviruntime.h>
#include <vector>

class XFeatTPU {
public:
  explicit XFeatTPU(const char *path);
  ~XFeatTPU();
  void detect(const cv::Mat &image, const cv::Mat &mask, int top_k,
              std::vector<cv::KeyPoint> &points, cv::Mat &descriptors);
private:
  CVI_MODEL_HANDLE model_ = nullptr;
  CVI_TENSOR *inputs_ = nullptr, *outputs_ = nullptr;
  int input_count_ = 0, output_count_ = 0;
  int width_ = 0, height_ = 0;
};

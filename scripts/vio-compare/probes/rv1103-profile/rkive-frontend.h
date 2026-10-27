#pragma once
#include <opencv2/core.hpp>
#include <vector>
namespace rkive_frontend {
// OPENVINS_RKIVE_MODE: software, software4, pyramid, lk, both.
void pyramid(const cv::Mat &image, std::vector<cv::Mat> &result, cv::Size window, int levels);
void flow(const std::vector<cv::Mat> &previous, const std::vector<cv::Mat> &current,
          const std::vector<cv::Point2f> &points, std::vector<cv::Point2f> &next,
          std::vector<unsigned char> &status, std::vector<float> &error,
          cv::Size window, int levels, cv::TermCriteria criteria, int flags);
}

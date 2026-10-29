#pragma once
#include <opencv2/core.hpp>
#include <vector>
namespace ov_core { namespace qr7 {
bool enabled();
cv::Mat find(const std::vector<cv::Point2f>& a,const std::vector<cv::Point2f>& b,
             double threshold,double confidence,int max_iterations,cv::OutputArray mask);
cv::Mat seven_point(const cv::Mat& a,const cv::Mat& b);
}}

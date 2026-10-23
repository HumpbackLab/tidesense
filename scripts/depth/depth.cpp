// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth.hpp"
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>
#include <cmath>
#include <stdexcept>
#include <time.h>

namespace {
void mark(DepthTiming* timing, int stage) {
  if (!timing) return;
  timespec t{};
  if (clock_gettime(CLOCK_MONOTONIC,&t)) throw std::runtime_error("Profile wall clock failed");
  timing->wall[stage]=uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) throw std::runtime_error("Profile CPU clock failed");
  timing->cpu[stage]=uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
cv::Mat matrix(const YAML::Node& n, int rows, int cols) {
  if (n["rows"].as<int>() != rows || n["cols"].as<int>() != cols ||
      !n["data"].IsSequence() || n["data"].size() != size_t(rows*cols))
    throw std::runtime_error("Invalid calibration matrix shape");
  cv::Mat m(rows, cols, CV_64F);
  for (int i = 0; i < rows*cols; ++i) {
    double v = n["data"][i].as<double>();
    if (!std::isfinite(v)) throw std::runtime_error("Nonfinite calibration");
    m.at<double>(i/cols, i%cols) = v;
  }
  return m;
}
cv::Mat transform(const YAML::Node& n) {
  auto t = matrix(n, 4, 4);
  auto r = t(cv::Rect(0,0,3,3));
  if (cv::norm(r*r.t()-cv::Mat::eye(3,3,CV_64F)) > 1e-5 ||
      std::abs(cv::determinant(r)-1) > 1e-5 ||
      cv::norm(t.row(3)-(cv::Mat_<double>(1,4) << 0,0,0,1)) > 1e-8)
    throw std::runtime_error("Invalid rigid camera transform");
  return t;
}
}

StereoDepth::StereoDepth(const std::string& calibration, int width, int disparities) {
  if ((width != 320 && width != 640) || disparities < 16 || disparities%16 || disparities >= width-15)
    throw std::runtime_error("Use width 320/640 and disparities a multiple of 16 below width-15");
  const cv::Size raw(640,400), size(width, width*5/8);
  auto n = YAML::LoadFile(calibration);
  if (!n["cameras"].IsSequence() || n["cameras"].size() != 2)
    throw std::runtime_error("Expected SchurVINS stereo calibration");
  cv::Mat k[2], d[2], t[2];
  std::string model;
  for (int i = 0; i < 2; ++i) {
    const auto c = n["cameras"][i]["camera"];
    if (c["type"].as<std::string>() != "pinhole" || c["image_width"].as<int>() != 640 ||
        c["image_height"].as<int>() != 400)
      throw std::runtime_error("Depth requires calibrated 640x400 pinhole cameras");
    const auto intr = matrix(c["intrinsics"],4,1);
    if (intr.at<double>(0) <= 0 || intr.at<double>(1) <= 0)
      throw std::runtime_error("Invalid camera focal length");
    k[i] = (cv::Mat_<double>(3,3) << intr.at<double>(0),0,intr.at<double>(2),
             0,intr.at<double>(1),intr.at<double>(3),0,0,1);
    d[i] = matrix(c["distortion"]["parameters"],4,1);
    auto type = c["distortion"]["type"].as<std::string>();
    if ((type != "radial-tangential" && type != "equidistant") || (i && type != model))
      throw std::runtime_error("Use matching radtan or equidistant distortion models");
    model = type;
    t[i] = transform(n["cameras"][i]["T_B_C"]);
  }
  // T_C1_C0 maps physical-left coordinates into physical-right coordinates.
  cv::Mat relative = t[1].inv()*t[0];
  cv::Mat s = (cv::Mat_<double>(3,3) << -1,0,0,0,-1,0,0,0,1);
  cv::Mat r = s*relative(cv::Rect(0,0,3,3));
  cv::Mat translation = s*relative(cv::Rect(3,0,1,3));
  if (cv::norm(r-cv::Mat::eye(3,3,CV_64F)) > 0.5 || cv::norm(translation) < 0.001 || cv::norm(translation) > 1)
    throw std::runtime_error("Expected TideSense opposite camera orientation and a metric stereo baseline");
  // Virtually rotate the right sensor 180 degrees, then fold this into its map.
  k[1].at<double>(0,2) = 639-k[1].at<double>(0,2);
  k[1].at<double>(1,2) = 399-k[1].at<double>(1,2);
  if (model == "radial-tangential") {
    d[1].at<double>(2) *= -1; d[1].at<double>(3) *= -1;
  }
  cv::Mat rotations[2], p[2], q;
  if (model == "equidistant")
    cv::fisheye::stereoRectify(k[0],d[0],k[1],d[1],raw,r,translation,
      rotations[0],rotations[1],p[0],p[1],q,cv::CALIB_ZERO_DISPARITY,size,0.,1.);
  else
    cv::stereoRectify(k[0],d[0],k[1],d[1],raw,r,translation,
      rotations[0],rotations[1],p[0],p[1],q,cv::CALIB_ZERO_DISPARITY,0.,size);
  if (!cv::checkRange(p[0]) || !cv::checkRange(p[1]) || p[0].at<double>(0,0) <= 0 ||
      std::abs(p[1].at<double>(0,3)) < 1e-6 || std::abs(p[1].at<double>(1,3)) > 1e-6)
    throw std::runtime_error("StereoBM requires a finite horizontal rectification");
  info_.width = size.width; info_.height = size.height;
  info_.reference = p[1].at<double>(0,3) > 0 ? 1 : 0;
  info_.fx = p[0].at<double>(0,0); info_.fy = p[0].at<double>(1,1);
  info_.cx = p[0].at<double>(0,2); info_.cy = p[0].at<double>(1,2);
  info_.baseline = std::abs(p[1].at<double>(0,3)/info_.fx);
  for (int i = 0; i < 2; ++i) {
    cv::Mat mx,my;
    if (model == "equidistant")
      cv::fisheye::initUndistortRectifyMap(k[i],d[i],rotations[i],p[i],size,CV_32FC1,mx,my);
    else
      cv::initUndistortRectifyMap(k[i],d[i],rotations[i],p[i],size,CV_32FC1,mx,my);
    if (i == 1) { mx = 639-mx; my = 399-my; }
    valid_[i] = (mx >= 0) & (mx < 639) & (my >= 0) & (my < 399);
    // Matching support and XSobel prefilter must stay within real source pixels.
    cv::erode(valid_[i],valid_[i],cv::Mat::ones(19,19,CV_8U),cv::Point(-1,-1),1,
              cv::BORDER_CONSTANT,cv::Scalar(0));
    cv::convertMaps(mx,my,map1_[i],map2_[i],CV_16SC2);
  }
  bm_ = cv::StereoBM::create(disparities,15);
  bm_->setMinDisparity(0); bm_->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);
  bm_->setPreFilterCap(31); bm_->setTextureThreshold(10); bm_->setUniquenessRatio(15);
  bm_->setSpeckleWindowSize(100); bm_->setSpeckleRange(2); bm_->setDisp12MaxDiff(1);
}

cv::Mat StereoDepth::millimetres(const cv::Mat& disparity, double fx_baseline,
                               const cv::Mat& reference_valid, const cv::Mat& target_valid) {
  if (disparity.type() != CV_16SC1 || reference_valid.type() != CV_8UC1 ||
      target_valid.type() != CV_8UC1 || disparity.size() != reference_valid.size() ||
      disparity.size() != target_valid.size() || !std::isfinite(fx_baseline) || fx_baseline <= 0)
    throw std::runtime_error("Invalid disparity conversion input");
  cv::Mat depth(disparity.size(),CV_16UC1,cv::Scalar(0));
  const double scale = fx_baseline*16000.;
  for (int y = 0; y < disparity.rows; ++y) {
    const auto d = disparity.ptr<int16_t>(y);
    const auto good = reference_valid.ptr<uint8_t>(y), other = target_valid.ptr<uint8_t>(y);
    auto z = depth.ptr<uint16_t>(y);
    for (int x = 0; x < disparity.cols; ++x) {
      if (d[x] <= 0 || !good[x]) continue;
      const double matched = x-d[x]/16.;
      const int lo = int(std::floor(matched)), hi = int(std::ceil(matched));
      if (lo < 0 || hi >= disparity.cols || !other[lo] || !other[hi]) continue;
      const double mm = scale/d[x];
      if (mm >= 1 && mm <= 65535) z[x] = uint16_t(std::lround(mm));
    }
  }
  return depth;
}

cv::Mat StereoDepth::compute(const cv::Mat& left, const cv::Mat& right, DepthTiming* timing) {
  if (left.type() != CV_8UC1 || right.type() != CV_8UC1 ||
      left.size() != cv::Size(640,400) || right.size() != left.size())
    throw std::runtime_error("Expected original 640x400 mono8 stereo images");
  mark(timing,0);
  cv::remap(left,rect_[0],map1_[0],map2_[0],cv::INTER_LINEAR,cv::BORDER_CONSTANT);
  cv::remap(right,rect_[1],map1_[1],map2_[1],cv::INTER_LINEAR,cv::BORDER_CONSTANT);
  const int ref = info_.reference;
  mark(timing,1);
  bm_->compute(rect_[ref],rect_[1-ref],disparity_);
  mark(timing,2);
  auto depth=millimetres(disparity_,info_.fx*info_.baseline,valid_[ref],valid_[1-ref]);
  mark(timing,3);
  return depth;
}

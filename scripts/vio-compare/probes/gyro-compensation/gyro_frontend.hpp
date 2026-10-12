#pragma once

// Standalone replay experiment; production builds leave this macro disabled.
#ifndef OPENVINS_GYRO_FRONTEND
#define OPENVINS_GYRO_FRONTEND 0
#endif

#if OPENVINS_GYRO_FRONTEND
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include "cam/CamEqui.h"

namespace ov_core { namespace gyro_frontend {

struct RayCache {
  int width = 0, height = 0;
  Eigen::MatrixXd intrinsics;
  std::vector<cv::Point3f> rays;
};

struct Context {
  // Replay adapter uses one OpenCV thread and one VIO instance.
  std::map<size_t, Eigen::Matrix3d> rotations;
  std::map<size_t, RayCache> caches;
  double timestamp = 0;
  int mode = 0;
  Context() {
    const char *s = std::getenv("OPENVINS_GYRO_MODE");
    if (s && std::string(s) == "initial") mode = 1;
    if (s && std::string(s) == "warp") mode = 2;
  }
};

inline Context &context() { static Context c; return c; }
inline void set_rotations(const std::map<size_t, Eigen::Matrix3d> &rotations, double timestamp) {
  context().rotations = rotations;
  context().timestamp = timestamp;
}

inline bool match(size_t cam_id, const std::shared_ptr<CamBase> &cam,
                  const std::vector<cv::Mat> &previous, const std::vector<cv::Mat> &current,
                  const std::vector<cv::Point2f> &old_points, std::vector<cv::Point2f> &new_points,
                  std::vector<uchar> &status, std::vector<float> &error,
                  cv::Size window, int levels, cv::TermCriteria criteria) {
  Context &ctx = context();
  auto rotation = ctx.rotations.find(cam_id);
  if (!ctx.mode || rotation == ctx.rotations.end()) return false;
  const auto start = std::chrono::steady_clock::now();
  const Eigen::Matrix3d &R = rotation->second;
  const int width = current[0].cols, height = current[0].rows;
  std::vector<cv::Point2f> lk_old, lk_new;
  std::vector<size_t> indices;
  cv::Mat warped, valid_eroded;
  std::vector<cv::Mat> warped_pyramid;

  if (ctx.mode == 2) {
    RayCache &cache = ctx.caches[cam_id];
    const Eigen::MatrixXd intrinsics = cam->get_value();
    if (cache.width != width || cache.height != height ||
        cache.intrinsics.size() != intrinsics.size() || !cache.intrinsics.isApprox(intrinsics, 1e-12)) {
      cache.width = width; cache.height = height; cache.intrinsics = intrinsics;
      std::vector<cv::Point2f> pixels, normalized;
      pixels.reserve(width * height);
      for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) pixels.emplace_back(x, y);
      if (std::dynamic_pointer_cast<CamEqui>(cam))
        cv::fisheye::undistortPoints(pixels, normalized, cam->get_K(), cam->get_D());
      else cv::undistortPoints(pixels, normalized, cam->get_K(), cam->get_D());
      cache.rays.clear(); cache.rays.reserve(normalized.size());
      for (const auto &q : normalized) cache.rays.emplace_back(q.x, q.y, 1.f);
    }
    // Destination pixels are in the current camera orientation. Sample the
    // previous raw image using the inverse rotation, retaining fisheye geometry.
    std::vector<cv::Point3f> source_rays; source_rays.reserve(cache.rays.size());
    const Eigen::Matrix3f inverse = R.transpose().cast<float>();
    for (const auto &q : cache.rays) {
      Eigen::Vector3f r = inverse * Eigen::Vector3f(q.x, q.y, q.z);
      source_rays.emplace_back(r.x(), r.y(), r.z());
    }
    std::vector<cv::Point2f> source_pixels;
    if (std::dynamic_pointer_cast<CamEqui>(cam))
      cv::fisheye::projectPoints(source_rays, source_pixels, cv::Vec3d(), cv::Vec3d(), cam->get_K(), cam->get_D());
    else cv::projectPoints(source_rays, cv::Vec3d(), cv::Vec3d(), cam->get_K(), cam->get_D(), source_pixels);
    cv::Mat map_x(height, width, CV_32F), map_y(height, width, CV_32F), valid(height, width, CV_8U);
    for (size_t i = 0; i < source_pixels.size(); ++i) {
      const auto &q = source_pixels[i];
      bool ok = source_rays[i].z > 1e-4f && std::isfinite(q.x) && std::isfinite(q.y) &&
                q.x >= 1 && q.y >= 1 && q.x < width - 2 && q.y < height - 2;
      map_x.ptr<float>()[i] = ok ? q.x : -1;
      map_y.ptr<float>()[i] = ok ? q.y : -1;
      valid.ptr<uchar>()[i] = ok ? 255 : 0;
    }
    cv::remap(previous[0], warped, map_x, map_y, cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    cv::erode(valid, valid_eroded, cv::getStructuringElement(cv::MORPH_RECT, window),
              cv::Point(-1, -1), 1, cv::BORDER_CONSTANT, cv::Scalar(0));
    cv::buildOpticalFlowPyramid(warped, warped_pyramid, window, levels, true,
                               cv::BORDER_REFLECT_101, cv::BORDER_CONSTANT, false);
  }

  for (size_t i = 0; i < old_points.size(); ++i) {
    cv::Point2f n = cam->undistort_cv(old_points[i]);
    Eigen::Vector3d ray = R * Eigen::Vector3d(n.x, n.y, 1.);
    if (ray.z() <= 1e-4) continue;
    cv::Point2f predicted = cam->distort_cv(cv::Point2f(ray.x() / ray.z(), ray.y() / ray.z()));
    if (!std::isfinite(predicted.x) || !std::isfinite(predicted.y) ||
        predicted.x < 0 || predicted.y < 0 || predicted.x >= width || predicted.y >= height) continue;
    if (ctx.mode == 2 && !valid_eroded.at<uchar>((int)predicted.y, (int)predicted.x)) continue;
    indices.push_back(i);
    lk_old.push_back(ctx.mode == 2 ? predicted : old_points[i]);
    lk_new.push_back(predicted);
  }
  status.assign(old_points.size(), 0);
  error.assign(old_points.size(), std::numeric_limits<float>::quiet_NaN());
  if (!indices.empty()) {
    std::vector<uchar> small_status; std::vector<float> small_error;
    cv::calcOpticalFlowPyrLK(ctx.mode == 2 ? warped_pyramid : previous, current,
                            lk_old, lk_new, small_status, small_error, window, levels, criteria,
                            cv::OPTFLOW_USE_INITIAL_FLOW);
    for (size_t i = 0; i < indices.size(); ++i) {
      new_points[indices[i]] = lk_new[i];
      status[indices[i]] = small_status[i];
      error[indices[i]] = small_error[i];
    }
  }
  if (std::getenv("OPENVINS_GYRO_PROFILE"))
    std::fprintf(stderr, "GYRO_FRONT %.9f %zu %d %zu %zu %.6f\n", ctx.timestamp, cam_id, ctx.mode,
                 old_points.size(), indices.size(), std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start).count());
  // Only LK uses rotated template coordinates. RANSAC and the feature database
  // must continue to receive the original previous-frame coordinates.
  return true;
}
} }
#endif

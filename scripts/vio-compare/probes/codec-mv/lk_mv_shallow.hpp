#pragma once
#include <chrono>
#include <opencv2/video/tracking.hpp>
#include "codec_mv.hpp"
#if OPENVINS_CODEC_MV_SKIP_LK
#include "lk_mv_verify.hpp"
#endif

namespace ov_codec_mv {
// Only called for same-camera temporal matching. Keep geometry checks outside.
inline void match(const std::vector<cv::Mat> &previous, const std::vector<cv::Mat> &current,
                  const std::vector<cv::Point2f> &p0, std::vector<cv::Point2f> &p1,
                  std::vector<uchar> &status, std::vector<float> &error, cv::Size window,
                  int levels, cv::TermCriteria termination, const std::vector<uchar> &seeded) {
  auto &mv = state();
  mv.lk_stats = State::LKStats{};
  mv.lk_stats.path.assign(p0.size(), 0);
  mv.lk_stats.failure.assign(p0.size(), 0);
  using Clock = std::chrono::steady_clock;
  auto elapsed = [](Clock::time_point a) {
    return std::chrono::duration<double, std::milli>(Clock::now()-a).count();
  };
  if (mv.mode!="off" && mv.lk_mode=="verify") {
#if OPENVINS_CODEC_MV_SKIP_LK
    verify_match(previous,current,p0,p1,status,error,window,levels,termination,seeded);
    return;
#else
    throw std::runtime_error("verify requires OPENVINS_CODEC_MV_SKIP_LK=1");
#endif
  }
  if (mv.mode == "off" || mv.lk_mode == "full") {
    const auto start = Clock::now();
    cv::calcOpticalFlowPyrLK(previous, current, p0, p1, status, error, window, levels,
                            termination, cv::OPTFLOW_USE_INITIAL_FLOW);
    mv.lk_stats.full = p0.size(); mv.lk_stats.full_ms = elapsed(start);
    mv.lk_stats.valid = status;
    return;
  }
  static const double patch_limit = std::getenv("OV_CODEC_MV_PATCH_LIMIT") ?
      std::stod(std::getenv("OV_CODEC_MV_PATCH_LIMIT")) : 20.0;
  static const double refine_limit = std::getenv("OV_CODEC_MV_REFINE_LIMIT") ?
      std::stod(std::getenv("OV_CODEC_MV_REFINE_LIMIT")) : 6.0;
  std::vector<size_t> shallow_indices, full_indices;
  std::vector<cv::Point2f> a, b;
  for (size_t i = 0; i < p0.size(); ++i) {
    if (seeded[i]) { shallow_indices.push_back(i); a.push_back(p0[i]); b.push_back(p1[i]); }
    else full_indices.push_back(i);
  }
  status.assign(p0.size(), 0); error.assign(p0.size(), 0);
  std::vector<uchar> valid;
  std::vector<float> residual;
  if (!a.empty()) {
    const auto start = Clock::now();
    cv::calcOpticalFlowPyrLK(previous, current, a, b, valid, residual, window, 1,
                            termination, cv::OPTFLOW_USE_INITIAL_FLOW);
    mv.lk_stats.shallow_ms = elapsed(start);
    mv.lk_stats.shallow = a.size();
    for (size_t k = 0; k < a.size(); ++k) {
      size_t i = shallow_indices[k];
      const auto &q = b[k];
      const uchar failure = !valid[k] ? 1 :
        (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(residual[k])) ? 2 :
        residual[k] > patch_limit ? 3 : cv::norm(q-p1[i]) > refine_limit ? 4 :
        (q.x < 0 || q.y < 0 || q.x >= current[0].cols || q.y >= current[0].rows) ? 5 : 0;
      if (!failure) {
        mv.lk_stats.path[i] = 1;
        p1[i] = q; status[i] = valid[k]; error[i] = residual[k];
      }
      else {
        mv.lk_stats.path[i] = 2; mv.lk_stats.failure[i] = failure;
        if (mv.retry) { full_indices.push_back(i); ++mv.lk_stats.retries; }
        p1[i] = p0[i]; // A poor proposal must not constrain the rescue search.
      }
    }
  }
  a.clear(); b.clear();
  for (auto i : full_indices) { a.push_back(p0[i]); b.push_back(p1[i]); }
  if (!a.empty()) {
    const auto start = Clock::now();
    cv::calcOpticalFlowPyrLK(previous, current, a, b, valid, residual, window, levels,
                            termination, cv::OPTFLOW_USE_INITIAL_FLOW);
    mv.lk_stats.full_ms = elapsed(start); mv.lk_stats.full = a.size();
    for (size_t k = 0; k < a.size(); ++k) {
      size_t i = full_indices[k];
      p1[i] = b[k]; status[i] = valid[k]; error[i] = residual[k];
    }
  }
  mv.lk_stats.valid = status;
}
} // namespace ov_codec_mv

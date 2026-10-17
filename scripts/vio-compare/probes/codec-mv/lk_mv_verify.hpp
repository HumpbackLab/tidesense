#pragma once
#include <array>
#include <chrono>
#include <opencv2/video/tracking.hpp>
#include "codec_mv.hpp"

namespace ov_codec_mv {
struct VerifyParameters {
  float mae = 4, texture = 10;
  int hamming = 3, refresh = 5;
  VerifyParameters() {
    if (const char *s=std::getenv("OV_CODEC_MV_VERIFY_MAE")) mae=std::stof(s);
    if (const char *s=std::getenv("OV_CODEC_MV_VERIFY_TEXTURE")) texture=std::stof(s);
    if (const char *s=std::getenv("OV_CODEC_MV_VERIFY_HAMMING")) hamming=std::stoi(s);
    if (const char *s=std::getenv("OV_CODEC_MV_VERIFY_REFRESH")) refresh=std::stoi(s);
    if (!std::isfinite(mae) || mae<=0 || !std::isfinite(texture) || texture<0 ||
        hamming<0 || hamming>64 || refresh<0)
      throw std::runtime_error("Invalid codec MV verification thresholds");
  }
};

// 64 bilinear samples in a 15x15 footprint; no position search or LK iteration.
inline bool sample_patch(const cv::Mat &image,const cv::Point2f &p,std::array<float,64> &patch) {
  if (image.type()!=CV_8UC1 || !std::isfinite(p.x) || !std::isfinite(p.y) ||
      p.x<7 || p.y<7 || p.x>=image.cols-8 || p.y>=image.rows-8) return false;
  const int ix=int(std::floor(p.x)),iy=int(std::floor(p.y));
  const float fx=p.x-ix,fy=p.y-iy;
  size_t k=0;
  for (int y=-7;y<=7;y+=2) {
    const auto *a=image.ptr<uchar>(iy+y),*b=image.ptr<uchar>(iy+y+1);
    for (int x=-7;x<=7;x+=2) {
      const int col=ix+x;
      patch[k++]=(1-fy)*((1-fx)*a[col]+fx*a[col+1])+fy*((1-fx)*b[col]+fx*b[col+1]);
    }
  }
  return true;
}

// Failure codes 6..10: bounds, low texture, photometric residual, census, periodic refresh.
inline uchar verify_patch(const cv::Mat &previous,const cv::Mat &current,
                          const cv::Point2f &p,const cv::Point2f &q,
                          const VerifyParameters &cfg,float &error) {
  std::array<float,64> a,b;
  if (!sample_patch(previous,p,a) || !sample_patch(current,q,b)) return 6;
  float ma=0,mb=0;
  for (size_t k=0;k<a.size();++k) {ma+=a[k];mb+=b[k];}
  ma/=64;mb/=64;
  float va=0,vb=0,residual=0;
  int different=0;
  for (size_t k=0;k<a.size();++k) {
    float da=a[k]-ma,db=b[k]-mb;
    va+=da*da;vb+=db*db;
    residual+=std::abs(da-db);
    different+=(da>0)!=(db>0);
  }
  error=residual/64;
  if (std::min(va,vb)<64*cfg.texture*cfg.texture) return 7;
  if (error>cfg.mae) return 8;
  if (different>cfg.hamming) return 9;
  return 0;
}

inline void verify_match(const std::vector<cv::Mat> &previous,const std::vector<cv::Mat> &current,
                         const std::vector<cv::Point2f> &p0,std::vector<cv::Point2f> &p1,
                         std::vector<uchar> &status,std::vector<float> &error,cv::Size window,
                         int levels,cv::TermCriteria termination,const std::vector<uchar> &seeded) {
  using Clock=std::chrono::steady_clock;
  static const VerifyParameters cfg;
  auto &mv=state();auto &stats=mv.lk_stats;
  status.assign(p0.size(),0);error.assign(p0.size(),0);
  const bool refresh=cfg.refresh && (mv.frame_index-1)%cfg.refresh==0;
  std::vector<size_t> fallback;
  const auto start=Clock::now();
  for (size_t i=0;i<p0.size();++i) {
    uchar failure=0;
    if (seeded[i] && !refresh) {
      ++stats.verify_candidates;
      failure=verify_patch(previous[0],current[0],p0[i],p1[i],cfg,error[i]);
      if (!failure) {
        status[i]=1;stats.path[i]=3;++stats.verified;
        continue;
      }
    } else if (seeded[i] && refresh) { failure=10;++stats.refreshed; }
    stats.failure[i]=failure;
    fallback.push_back(i);
    p1[i]=p0[i]; // Keep the baseline zero-flow initialization for untrusted points.
  }
  stats.verify_ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  std::vector<cv::Point2f> a,b;
  for (auto i:fallback) {a.push_back(p0[i]);b.push_back(p1[i]);}
  if (!a.empty()) {
    std::vector<uchar> valid;std::vector<float> residual;
    const auto full_start=Clock::now();
    cv::calcOpticalFlowPyrLK(previous,current,a,b,valid,residual,window,levels,
                            termination,cv::OPTFLOW_USE_INITIAL_FLOW);
    stats.full_ms=std::chrono::duration<double,std::milli>(Clock::now()-full_start).count();
    for (size_t k=0;k<a.size();++k) {
      auto i=fallback[k];p1[i]=b[k];status[i]=valid[k];error[i]=residual[k];
    }
  }
  stats.full=fallback.size();stats.valid=status;
}
} // namespace ov_codec_mv

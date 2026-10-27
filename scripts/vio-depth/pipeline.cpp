// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline.hpp"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <time.h>
#include <stdexcept>
#include <arm_neon.h>
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
uint64_t cpu() { timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) throw std::runtime_error("CPU clock");return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec; }
cv::Mat prefilter(const cv::Mat& raw) {
  cv::Mat result(raw.size(),CV_8U);
  for(int y=0;y<raw.rows;++y) {
    auto a=raw.ptr<uint8_t>(y?y-1:1),b=raw.ptr<uint8_t>(y),c=raw.ptr<uint8_t>(y+1<raw.rows?y+1:y-1);
    auto out=result.ptr<uint8_t>(y);out[0]=out[raw.cols-1]=31;int x=1;
    for(;x+8<raw.cols;x+=8) {
      auto dx=[&](const uint8_t* r) { return vreinterpretq_s16_u16(vsubl_u8(vld1_u8(r+x+1),vld1_u8(r+x-1))); };
      auto sum=vaddq_s16(vaddq_s16(dx(a),dx(c)),vshlq_n_s16(dx(b),1));
      sum=vmaxq_s16(vdupq_n_s16(-31),vminq_s16(vdupq_n_s16(31),sum));
      vst1_u8(out+x,vmovn_u16(vreinterpretq_u16_s16(vaddq_s16(sum,vdupq_n_s16(31)))));
    }
    for(;x<raw.cols-1;++x) {
      int v=int(a[x+1])-a[x-1]+2*(int(b[x+1])-b[x-1])+int(c[x+1])-c[x-1];
      out[x]=std::max(-31,std::min(31,v))+31;
    }
  }
  return result;
}
}
DepthPipeline::DepthPipeline(const std::string& calibration):geometry_(calibration,320,32,true),match_(320,200,32,64,6,true,true) {}
cv::Mat DepthPipeline::quality(const SadResult& result,const cv::Mat& filtered,int nd) {
  cv::Mat texture,energy;cv::absdiff(filtered,cv::Scalar(31),energy);
  cv::boxFilter(energy,texture,CV_16U,cv::Size(15,15),cv::Point(-1,-1),false,cv::BORDER_CONSTANT);
  cv::Mat disparity(filtered.size(),CV_16S,cv::Scalar(-16));
  for(int y=7;y<filtered.rows-7;++y) {
    auto dst=disparity.ptr<int16_t>(y);auto d=result.disparity.ptr<uint8_t>(y);
    auto c=result.cost.ptr<uint16_t>(y),m=result.minus.ptr<uint16_t>(y),p=result.plus.ptr<uint16_t>(y);
    auto other=result.competitor.ptr<uint16_t>(y);auto tex=texture.ptr<uint16_t>(y);
    for(int x=nd-1+7;x<filtered.cols-7;++x) {
      if(d[x]==0 || d[x]>=nd-1 || tex[x]<10 || other[x]<=c[x]+c[x]*15/100) continue;
      int denom=int(m[x])+p[x]-2*c[x]+std::abs(int(m[x])-p[x]);
      // Same Q4 parabolic interpolation convention as OpenCV StereoBM.
      dst[x]=(d[x]*256+(int(m[x])-p[x])*256/(denom?denom:1)+15)>>4;
    }
  }
  // Cost-guided reverse warp consistency, as used by StereoBM (not a second full match).
  cv::Mat signed_cost;result.cost.convertTo(signed_cost,CV_16S);
  cv::validateDisparity(disparity,signed_cost,0,nd,1);
  cv::filterSpeckles(disparity,-16,100,32);
  return disparity;
}
std::array<cv::Mat,2> DepthPipeline::prepare(const cv::Mat& left,const cv::Mat& right,PipelineTiming& timing) {
  timing={};uint64_t cpu_start=cpu();auto start=Clock::now();SadTiming down;
  auto reduced=downsample_.run(left,right,down);auto a=Clock::now();
  auto rect=geometry_.rectify(reduced[0],reduced[1]);auto b=Clock::now();
  int ref=geometry_.intrinsics().reference;
  std::array<cv::Mat,2> filtered{{prefilter(rect[ref]),prefilter(rect[1-ref])}};
  timing.downsample=elapsed(start,a);timing.rectify=elapsed(a,b);timing.prefilter=elapsed(b,Clock::now());
  timing.cpu=(cpu()-cpu_start)*1e-6;return filtered;
}
SadResult DepthPipeline::match(const std::array<cv::Mat,2>& filtered,PipelineTiming& timing) {
  uint64_t cpu_start=cpu();auto start=Clock::now();SadTiming match;
  auto result=match_.run(filtered[0],filtered[1],match);
  timing.match=elapsed(start,Clock::now());timing.cpu+=(cpu()-cpu_start)*1e-6;return result;
}
cv::Mat DepthPipeline::finish(const SadResult& result,const cv::Mat& filtered,PipelineTiming& timing) {
  uint64_t cpu_start=cpu();auto start=Clock::now();
  auto q4=quality(result,filtered,32);const auto& i=intrinsics();int ref=i.reference;
  auto depth=StereoDepth::millimetres(q4,i.fx*i.baseline,geometry_.valid(ref),geometry_.valid(1-ref));
  timing.filter=elapsed(start,Clock::now());timing.cpu+=(cpu()-cpu_start)*1e-6;return depth;
}
cv::Mat DepthPipeline::compute(const cv::Mat& left,const cv::Mat& right,PipelineTiming& timing) {
  auto filtered=prepare(left,right,timing);auto result=match(filtered,timing);return finish(result,filtered[0],timing);
}

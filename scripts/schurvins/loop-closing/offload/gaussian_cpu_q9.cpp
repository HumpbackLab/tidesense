// SPDX-License-Identifier: GPL-3.0-or-later
// Numerical control for the TPU experiment: identical Q9 coefficients/rounding.
#include "gaussian_cpu_q9.hpp"
#include <stdexcept>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
namespace {
thread_local std::vector<uint8_t> scratch;
#if defined(__aarch64__)
inline uint8x8_t filter8(const uint8_t* p0,const uint8_t* p1,const uint8_t* p2,
                        const uint8_t* p3,const uint8_t* p4,const uint8_t* p5,const uint8_t* p6) {
  const uint16x8_t a=vaddl_u8(vld1_u8(p0),vld1_u8(p6));
  const uint16x8_t b=vaddl_u8(vld1_u8(p1),vld1_u8(p5));
  const uint16x8_t c=vaddl_u8(vld1_u8(p2),vld1_u8(p4));
  const uint16x8_t d=vmovl_u8(vld1_u8(p3));
  uint32x4_t low=vmull_n_u16(vget_low_u16(a),36),high=vmull_n_u16(vget_high_u16(a),36);
  low=vmlal_n_u16(low,vget_low_u16(b),67);high=vmlal_n_u16(high,vget_high_u16(b),67);
  low=vmlal_n_u16(low,vget_low_u16(c),98);high=vmlal_n_u16(high,vget_high_u16(c),98);
  low=vmlal_n_u16(low,vget_low_u16(d),110);high=vmlal_n_u16(high,vget_high_u16(d),110);
  return vmovn_u16(vcombine_u16(vrshrn_n_u32(low,9),vrshrn_n_u32(high,9)));
}
#endif
}
void schur_gaussian_cpu_q9(cv::Mat& parent,const std::vector<cv::Rect>& layers) {
  constexpr unsigned k[7]={36,67,98,110,98,67,36};
  if(parent.type()!=CV_8UC1)throw std::runtime_error("CPU Q9 expects mono8");
  for(const auto& r:layers) {
    if(r.width<=0 || r.height<=0 || r.x<3 || r.y<3 || r.x+r.width+3>parent.cols || r.y+r.height+3>parent.rows)
      throw std::runtime_error("CPU Q9 needs the original ROI halo");
    const size_t needed=size_t(r.width)*(r.height+6);
    if(scratch.size()<needed)scratch.resize(needed);
    for(int y=0;y<r.height+6;++y) {
      const auto* source=parent.ptr<uint8_t>(r.y+y-3)+r.x;
      auto* target=scratch.data()+size_t(y)*r.width;int x=0;
#if defined(__aarch64__)
      for(;x+8<=r.width;x+=8)vst1_u8(target+x,filter8(source+x-3,source+x-2,source+x-1,
                                                   source+x,source+x+1,source+x+2,source+x+3));
#endif
      for(;x<r.width;++x){unsigned sum=0;for(int i=0;i<7;++i)sum+=source[x+i-3]*k[i];target[x]=(sum+256)>>9;}
    }
    for(int y=0;y<r.height;++y) {
      auto* target=parent.ptr<uint8_t>(r.y+y)+r.x;const auto* source=scratch.data()+size_t(y)*r.width;int x=0;
#if defined(__aarch64__)
      for(;x+8<=r.width;x+=8)vst1_u8(target+x,filter8(source+x,source+x+r.width,source+x+2*r.width,
          source+x+3*r.width,source+x+4*r.width,source+x+5*r.width,source+x+6*r.width));
#endif
      for(;x<r.width;++x){unsigned sum=0;for(int i=0;i<7;++i)sum+=source[x+i*r.width]*k[i];target[x]=(sum+256)>>9;}
    }
  }
}

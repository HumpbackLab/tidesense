// Standalone probe: sparse template-window Scharr, existing OpenCV NEON LK.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif
namespace window_lk {
constexpr int width=16,height=15,border=18;
struct Workspace {
  std::vector<cv::Mat> derivatives,previous;
  explicit Workspace(const std::vector<cv::Mat>& gray) {
    for(const auto& im:gray){
      cv::Mat allocation(im.rows+2*border,im.cols+2*border,CV_16SC2);
      derivatives.push_back(allocation(cv::Rect(border,border,im.cols,im.rows)));
      previous.push_back(im);previous.push_back(derivatives.back());
    }
  }
  // Only the fixed template support is needed: 16x15 plus interpolation's
  // extra row/column. Grey halo is REFLECT_101; derivative halo is zero.
  size_t prepare(const std::vector<cv::Mat>& gray,const std::vector<cv::Point2f>& points) {
    if(gray.size()!=derivatives.size())throw std::runtime_error("pyramid level mismatch");
    size_t pixels=0;
    for(size_t level=0;level<gray.size();++level){
      const auto& im=gray[level];auto& d=derivatives[level];previous[2*level]=im;
      const float scale=1.f/(1u<<level);
      for(const auto& p:points){
        int ix=cvFloor(p.x*scale-7.5f),iy=cvFloor(p.y*scale-7.f);
        if(ix < -width || ix>=im.cols || iy < -height || iy>=im.rows)continue;
        for(int y=iy;y<=iy+height;++y){
          auto *out=reinterpret_cast<short*>(d.data+ptrdiff_t(y)*ptrdiff_t(d.step))+2*ix;
          if(y<0 || y>=im.rows){std::memset(out,0,(width+1)*2*sizeof(short));continue;}
          const auto *top=im.data+ptrdiff_t(y-1)*ptrdiff_t(im.step);
          const auto *mid=im.data+ptrdiff_t(y)*ptrdiff_t(im.step);
          const auto *bot=im.data+ptrdiff_t(y+1)*ptrdiff_t(im.step);
          int x=ix;
          for(;x<0;++x){out[2*(x-ix)]=0;out[2*(x-ix)+1]=0;}
          int end=std::min(ix+width+1,im.cols);
#if defined(__ARM_NEON) || defined(__aarch64__)
          for(;x+8<=end;x+=8){
            auto load=[](const unsigned char *p){return vreinterpretq_s16_u16(vmovl_u8(vld1_u8(p)));};
            auto tl=load(top+x-1),tr=load(top+x+1),ml=load(mid+x-1),mr=load(mid+x+1);
            auto bl=load(bot+x-1),br=load(bot+x+1),tc=load(top+x),bc=load(bot+x);
            int16x8x2_t value;
            value.val[0]=vaddq_s16(vmulq_n_s16(vaddq_s16(vsubq_s16(tr,tl),vsubq_s16(br,bl)),3),vmulq_n_s16(vsubq_s16(mr,ml),10));
            value.val[1]=vaddq_s16(vmulq_n_s16(vaddq_s16(vsubq_s16(bl,tl),vsubq_s16(br,tr)),3),vmulq_n_s16(vsubq_s16(bc,tc),10));
            vst2q_s16(out+2*(x-ix),value);
          }
#endif
          for(;x<end;++x){
            out[2*(x-ix)]=3*(int(top[x+1])-top[x-1]+int(bot[x+1])-bot[x-1])+10*(int(mid[x+1])-mid[x-1]);
            out[2*(x-ix)+1]=3*(int(bot[x-1])-top[x-1]+int(bot[x+1])-top[x+1])+10*(int(bot[x])-top[x]);
          }
          for(;x<=ix+width;++x){out[2*(x-ix)]=0;out[2*(x-ix)+1]=0;}
        }
        pixels+=(width+1)*(height+1);
      }
    }
    return pixels;
  }
  void track(const std::vector<cv::Mat>& next,const std::vector<cv::Point2f>& points,
             std::vector<cv::Point2f>& output,std::vector<unsigned char>& valid) const {
    output=points;
    cv::calcOpticalFlowPyrLK(previous,next,points,output,valid,cv::noArray(),cv::Size(width,height),
       int(derivatives.size())-1,cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
  }
};
}

// SPDX-License-Identifier: GPL-3.0-or-later
// Optional approximate sigma=2 Gaussian: two Q9 seven-tap TPU filters.
#include "gaussian_jobs.h"
#include <opencv2/imgproc.hpp>
#include <cvi_sys.h>
#include <cviruntime_context.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <time.h>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned Guard=128,WeightsOffset=0;
unsigned align(unsigned n,unsigned by=64){return (n+by-1)/by*by;}
double ms(Clock::duration value){return std::chrono::duration<double,std::milli>(value).count();}
double cpu_ms(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
void check(int value,const char* what){if(value)throw std::runtime_error(std::string(what)+": "+std::to_string(value));}
struct Plan {
  CVI_U64 physical=0;void* memory=nullptr;unsigned bytes=0;
  CVI_RT_HANDLE runtime=nullptr;CVI_RT_MEM program=nullptr;
  std::vector<gaussian_level> levels;
  Plan(CVI_RT_HANDLE rt,const std::vector<cv::Rect>& rectangles):runtime(rt) {
    unsigned offset=256;
    for(const auto& rect:rectangles) {
      if(rect.width<1 || rect.width>640 || rect.height<1 || rect.height>480)
        throw std::runtime_error("TPU Gaussian supports layers up to 640x480");
      gaussian_level l{};l.width=rect.width;l.height=rect.height;
      const unsigned padded=align(l.width,8);
      l.input_stride=align(padded+6,16);l.output_stride=align(padded,16);
      l.input_offset=offset;offset=align(offset+l.input_stride*(l.height+6)+Guard);
      l.output_offset=offset;offset=align(offset+l.output_stride*l.height+Guard);
      levels.push_back(l);
    }
    bytes=offset;
    try {
      check(CVI_SYS_IonAlloc_Cached(&physical,&memory,"orb_gauss",bytes),"Gaussian ION allocate");
      if(!memory)throw std::runtime_error("Gaussian ION returned null");
      std::memset(memory,0xa5,bytes);
      const int8_t coefficients[7]={36,67,98,110,98,67,36}; // sum=512
      for(unsigned kernel=0;kernel<2;++kernel)for(unsigned channel=0;channel<8;++channel)
        std::memcpy(static_cast<uint8_t*>(memory)+kernel*56+channel*7,coefficients,7);
      check(CVI_SYS_IonFlushCache(physical,memory,bytes),"Gaussian ION initialize");
      uint8_t* commands=nullptr;uint32_t command_bytes=0;
      check(schur_gaussian_make_job(physical,levels.data(),levels.size(),WeightsOffset,
                                    &commands,&command_bytes),"Gaussian command generation");
      const int status=CVI_RT_LoadCmdbuf(runtime,commands,command_bytes,0,0,false,&program);
      std::free(commands);check(status,"Gaussian load program");
    }catch(...){cleanup();throw;}
  }
  ~Plan(){cleanup();}
  void cleanup(){if(program){CVI_RT_MemFree(runtime,program);program=nullptr;}
    if(memory){CVI_SYS_IonFree(physical,memory);memory=nullptr;}}
  bool matches(const std::vector<cv::Rect>& rects)const {
    if(rects.size()!=levels.size())return false;
    for(size_t i=0;i<levels.size();++i)if(unsigned(rects[i].width)!=levels[i].width ||
      unsigned(rects[i].height)!=levels[i].height)return false;
    return true;
  }
};
struct Gaussian {
  CVI_RT_HANDLE runtime=nullptr;
  std::vector<std::unique_ptr<Plan>> plans;
  size_t calls=0,layers=0,submissions=0,pixels=0,verified=0,mismatches=0,abs_error=0;
  unsigned max_error=0;bool reported=false;
  double init_ms=0,total_ms=0,pack_ms=0,submit_ms=0,unpack_ms=0,thread_ms=0,reference_ms=0;
  Gaussian(){check(CVI_RT_Init(&runtime),"Gaussian TPU runtime init");}
  ~Gaussian(){if(calls && !reported)report();plans.clear();if(runtime)CVI_RT_DeInit(runtime);}
  void report(){reported=true;std::cout<<"gaussian_offload: backend=tpu-q9 calls="<<calls<<" layers="<<layers
    <<" submissions="<<submissions<<" plans="<<plans.size()<<" pixels="<<pixels
    <<" init_ms="<<init_ms<<" total_ms="<<total_ms<<" pack_ms="<<pack_ms<<" submit_ms="<<submit_ms
    <<" unpack_ms="<<unpack_ms<<" thread_cpu_ms="<<thread_ms<<" verified_calls="<<verified
    <<" mismatch_pixels="<<mismatches<<" max_abs_error="<<max_error<<" abs_error_sum="<<abs_error
    <<" reference_ms="<<reference_ms<<'\n';}
  void run(cv::Mat& pyramid,const std::vector<cv::Rect>& rectangles) {
    if(pyramid.type()!=CV_8UC1 || rectangles.empty() || rectangles.size()>8)
      throw std::runtime_error("TPU Gaussian expects U8 pyramid with 1..8 layers");
    for(const auto& r:rectangles)if(r.x<3 || r.y<3 || r.x+r.width+3>pyramid.cols || r.y+r.height+3>pyramid.rows)
      throw std::runtime_error("TPU Gaussian requires the original three-pixel ROI halo");
    const char* verification=std::getenv("SCHUR_ORB_BLUR_VERIFY");
    if(verification && std::string(verification)!="0" && std::string(verification)!="1")
      throw std::runtime_error("SCHUR_ORB_BLUR_VERIFY must be 0 or 1");
    cv::Mat reference;
    if(verification && std::string(verification)=="1") {
      const auto start=Clock::now();reference=pyramid.clone();
      for(const auto& r:rectangles){cv::Mat roi=reference(r);cv::GaussianBlur(roi,roi,cv::Size(7,7),2,2,cv::BORDER_REFLECT_101);}
      reference_ms+=ms(Clock::now()-start);++verified;
    }
    const auto begin=Clock::now();const double cpu_begin=cpu_ms();
    Plan* plan=nullptr;
    for(auto& p:plans)if(p->matches(rectangles)){plan=p.get();break;}
    if(!plan) {
      if(plans.size()>=16)throw std::runtime_error("Too many Gaussian pyramid geometries");
      const auto start=Clock::now();plans.emplace_back(new Plan(runtime,rectangles));plan=plans.back().get();
      init_ms+=ms(Clock::now()-start);
    }
    auto* base=static_cast<uint8_t*>(plan->memory);
    auto start=Clock::now();
    for(size_t i=0;i<rectangles.size();++i) {
      const auto& r=rectangles[i];const auto& l=plan->levels[i];
      auto* input=base+l.input_offset;
      // Copy the existing parent halo, matching non-isolated OpenCV ROI semantics.
      for(unsigned y=0;y<l.height+6;++y) {
        std::memcpy(input+y*l.input_stride,pyramid.ptr<uint8_t>(r.y-3+y)+r.x-3,l.width+6);
        std::memset(input+y*l.input_stride+l.width+6,0,l.input_stride-l.width-6);
      }
      std::memset(base+l.output_offset+l.output_stride*l.height,0xa5,Guard);
      check(CVI_SYS_IonFlushCache(plan->physical+l.input_offset,input,l.input_stride*(l.height+6)),"Gaussian input flush");
      check(CVI_SYS_IonFlushCache(plan->physical+l.output_offset,base+l.output_offset,
                                 l.output_stride*l.height+Guard),"Gaussian output flush");
    }
    pack_ms+=ms(Clock::now()-start);
    start=Clock::now();check(CVI_RT_RunCmdbuf(runtime,plan->program,0,0),"Gaussian TPU execution");
    submit_ms+=ms(Clock::now()-start);++submissions;
    start=Clock::now();
    for(size_t i=0;i<rectangles.size();++i) {
      const auto& r=rectangles[i];const auto& l=plan->levels[i];auto* output=base+l.output_offset;
      check(CVI_SYS_IonInvalidateCache(plan->physical+l.output_offset,output,
                                       l.output_stride*l.height+Guard),"Gaussian output invalidate");
      for(unsigned k=0;k<Guard;++k)if(output[l.output_stride*l.height+k]!=0xa5)
        throw std::runtime_error("Gaussian output DMA guard changed");
      for(unsigned y=0;y<l.height;++y)std::memcpy(pyramid.ptr<uint8_t>(r.y+y)+r.x,output+y*l.output_stride,l.width);
      pixels+=size_t(l.width)*l.height;
    }
    unpack_ms+=ms(Clock::now()-start);++calls;layers+=rectangles.size();
    total_ms+=ms(Clock::now()-begin);thread_ms+=cpu_ms()-cpu_begin;
    if(!reference.empty())for(const auto& r:rectangles)for(int y=0;y<r.height;++y) {
      const auto* a=pyramid.ptr<uint8_t>(r.y+y)+r.x;const auto* b=reference.ptr<uint8_t>(r.y+y)+r.x;
      for(int x=0;x<r.width;++x){const unsigned d=std::abs(int(a[x])-int(b[x]));
        mismatches+=d!=0;abs_error+=d;max_error=std::max(max_error,d);}
    }
  }
};
thread_local std::unique_ptr<Gaussian> context;
}
bool tidesense_orb_blur(cv::Mat& pyramid,const std::vector<cv::Rect>& layers) {
  const char* mode=std::getenv("SCHUR_ORB_BLUR");
  if(mode && std::string(mode)=="cpu")return false;
  if(mode && std::string(mode)!="tpu-q9")throw std::runtime_error("SCHUR_ORB_BLUR must be cpu or tpu-q9");
  if(!context)context.reset(new Gaussian);
  context->run(pyramid,layers);return true;
}
extern "C" void tidesense_orb_blur_report(){if(context)context->report();}

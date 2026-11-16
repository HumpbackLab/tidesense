// SPDX-License-Identifier: GPL-3.0-or-later
// Optional detector variant: hardware Shi-Tomasi candidates, original ORB tail.
#include <opencv2/features2d.hpp>
#include <cvi_sys.h>
#include <linux/cvi_ive_ioctl.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t Pixels=640*480,Scratch=2*1024*1024,Guard=128;
struct Buffer {CVI_U64 physical=0;void* memory=nullptr;size_t bytes=0;};
void check(int value,const char* message) {if(value)throw std::runtime_error(std::string(message)+": "+std::to_string(value));}
struct IveOrb {
  Buffer input,output,scratch;
  int fd=-1;unsigned quality=3;
  size_t calls=0,candidates=0,pixels=0;
  double hardware_ms=0,total_ms=0;
  bool reported=false;
  IveOrb() {
    const auto parse_quality=[](const std::string& value) {
      size_t n=0;const unsigned long q=std::stoul(value,&n);
      if(n!=value.size() || !q || q>255)throw std::runtime_error("IVE quality must be 1..255");
      return unsigned(q);
    };
    if(const char* q=std::getenv("SCHUR_ORB_IVE_QUALITY")) {
      quality=parse_quality(q);
    }
    try {
      allocate(input,Pixels+Guard);allocate(output,Pixels+Guard);allocate(scratch,Scratch+Guard);
      fd=open("/dev/cvi-ive",O_RDWR);
      if(fd<0)throw std::runtime_error("Open Linux hardware IVE failed");
    }catch(...){cleanup();throw;}
  }
  ~IveOrb(){if(calls && !reported)report();cleanup();}
  void allocate(Buffer& b,size_t bytes) {
    b.bytes=bytes;check(CVI_SYS_IonAlloc_Cached(&b.physical,&b.memory,"orb_ive",bytes),"IVE ORB ION");
    if(!b.memory)throw std::runtime_error("IVE ORB allocation returned null");
    std::memset(b.memory,0xa5,bytes);check(CVI_SYS_IonFlushCache(b.physical,b.memory,bytes),"IVE ORB initialize");
  }
  void cleanup() {
    if(fd>=0){close(fd);fd=-1;}
    for(Buffer* b:{&scratch,&output,&input})if(b->memory){CVI_SYS_IonFree(b->physical,b->memory);b->memory=nullptr;}
  }
  void report() {
    reported=true;
    std::cout<<"ive_orb: calls="<<calls<<" candidates="<<candidates<<" pixels="<<pixels
      <<" quality="<<quality<<" hardware_ms="<<hardware_ms<<" detector_wall_ms="<<total_ms<<'\n';
  }
  void detect(const cv::Mat& image,const cv::Mat& mask,std::vector<cv::KeyPoint>& keys) {
    const auto begin=Clock::now();
    if(image.type()!=CV_8UC1 || image.cols<32 || image.cols>640 || image.rows<32 || image.rows>480)
      throw std::runtime_error("IVE ORB supports 32..640 by 32..480 U8 pyramid levels");
    const unsigned width=(image.cols+15)&~15u,height=(image.rows+3)&~3u;
    const size_t bytes=size_t(width)*height;
    auto* source=static_cast<uint8_t*>(input.memory);
    auto* response=static_cast<uint8_t*>(output.memory);
    for(unsigned y=0;y<height;++y) {
      const auto* row=image.ptr<uint8_t>(std::min<unsigned>(y,image.rows-1));
      std::memcpy(source+y*width,row,image.cols);
      std::memset(source+y*width+image.cols,row[image.cols-1],width-image.cols);
    }
    std::memset(response+bytes,0xa5,Guard);
    check(CVI_SYS_IonFlushCache(input.physical,input.memory,bytes),"IVE ORB input flush");
    check(CVI_SYS_IonFlushCache(output.physical,output.memory,bytes+Guard),"IVE ORB output flush");
    struct cvi_ive_ioctl_stcandicorner request{};
    request.stSrc.enType=IVE_IMAGE_TYPE_U8C1;request.stSrc.u32Width=width;request.stSrc.u32Height=height;
    request.stSrc.u32Stride[0]=width;request.stSrc.u64PhyAddr[0]=input.physical;
    request.stSrc.u64VirAddr[0]=reinterpret_cast<uintptr_t>(input.memory);
    request.stDst=request.stSrc;request.stDst.u64PhyAddr[0]=output.physical;
    request.stDst.u64VirAddr[0]=reinterpret_cast<uintptr_t>(output.memory);
    request.stCtrl.stMem.u64PhyAddr=scratch.physical;
    request.stCtrl.stMem.u64VirAddr=reinterpret_cast<uintptr_t>(scratch.memory);
    request.stCtrl.stMem.u32Size=Scratch;request.stCtrl.u0q8QualityLevel=quality;request.bInstant=1;
    const auto hardware=Clock::now();
    if(ioctl(fd,CVI_IVE_IOC_STCandiCorner,&request)<0)throw std::runtime_error("Hardware IVE ST ioctl failed");
    hardware_ms+=std::chrono::duration<double,std::milli>(Clock::now()-hardware).count();
    check(CVI_SYS_IonInvalidateCache(output.physical,output.memory,bytes+Guard),"IVE ORB output invalidate");
    for(size_t i=0;i<Guard;++i)if(response[bytes+i]!=0xa5)
      throw std::runtime_error("IVE ORB output DMA guard changed");
    check(CVI_SYS_IonInvalidateCache(scratch.physical+Scratch,static_cast<uint8_t*>(scratch.memory)+Scratch,Guard),"IVE ORB scratch guard invalidate");
    for(size_t i=0;i<Guard;++i)if(static_cast<uint8_t*>(scratch.memory)[Scratch+i]!=0xa5)
      throw std::runtime_error("IVE ORB scratch DMA guard changed");
    keys.clear();
    for(int y=3;y<image.rows-3;++y)for(int x=3;x<image.cols-3;++x) {
      const unsigned v=response[y*width+x];
      if(!v || (!mask.empty() && !mask.at<uint8_t>(y,x)))continue;
      bool maximum=true;
      // Deterministic plateau tie: retain the first position in raster order.
      for(int dy=-1;dy<=1 && maximum;++dy)for(int dx=-1;dx<=1;++dx) {
        if(!dx && !dy)continue;
        const unsigned other=response[(y+dy)*width+x+dx];
        if(other>v || (other==v && (dy<0 || (!dy && dx<0)))){maximum=false;break;}
      }
      if(maximum)keys.emplace_back(float(x),float(y),7.f,-1.f,float(v));
    }
    ++calls;candidates+=keys.size();pixels+=size_t(image.cols)*image.rows;
    total_ms+=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
  }
};
thread_local std::unique_ptr<IveOrb> context;
}
bool tidesense_orb_ive_detect(const cv::Mat& image,const cv::Mat& mask,std::vector<cv::KeyPoint>& keys) {
  const char* kind=std::getenv("SCHUR_ORB_DETECTOR");
  if(!kind || std::string(kind)=="fast")return false;
  if(std::string(kind)!="ive-st")throw std::runtime_error("SCHUR_ORB_DETECTOR must be fast or ive-st");
  if(!context)context.reset(new IveOrb);
  context->detect(image,mask,keys);return true;
}
extern "C" void tidesense_orb_ive_report() {if(context)context->report();}

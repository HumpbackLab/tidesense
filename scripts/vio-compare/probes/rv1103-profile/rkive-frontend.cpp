#include "rkive-frontend.h"
#include "rk_mpi_ive.h"
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
int RVE_DRM_MmzAlloc(uint64_t*,void**,uint32_t);
int RVE_DRM_MmzFree(void*);
int RVE_DRM_MmzFlush_Start(void*);
int RVE_DRM_MmzFlush_End(void*);
}
namespace rkive_frontend {
namespace {
enum class Mode { Software, Software4, Pyramid, Lk, Both };
Mode mode() {
  static const Mode value=[] {
    const char *env=std::getenv("OPENVINS_RKIVE_MODE");
    std::string name=env?env:"software";
    std::fprintf(stderr,"RKIVE frontend mode=%s; hardware: levels=3, iterations=20, eps_q11=1, min_eig_q8=1\n",name.c_str());
    if(name=="software")return Mode::Software;
    if(name=="software4")return Mode::Software4;
    if(name=="pyramid")return Mode::Pyramid;
    if(name=="lk")return Mode::Lk;
    if(name=="both")return Mode::Both;
    throw std::runtime_error("Unknown OPENVINS_RKIVE_MODE");
  }();
  return value;
}
void check(int rc,const char *what) {
  if(rc)throw std::runtime_error(std::string(what)+" failed rc="+std::to_string(rc));
}
struct Memory {
  IVE_MEM_INFO_S value{};
  Memory()=default;
  Memory(const Memory&)=delete;
  void allocate(unsigned int size) {
    if(value.u32Size>=size)return;
    if(value.u64VirAddr)check(RVE_DRM_MmzFree(data()),"free CMA");
    value={};uint64_t phys;void *virt;
    check(RVE_DRM_MmzAlloc(&phys,&virt,size),"allocate CMA");
    value.u64PhyAddr=phys;value.u64VirAddr=(uintptr_t)virt;value.u32Size=size;
  }
  void *data() const {return reinterpret_cast<void*>(static_cast<uintptr_t>(value.u64VirAddr));}
  void begin() const {check(RVE_DRM_MmzFlush_Start(data()),"DMA CPU sync start");}
  void end() const {check(RVE_DRM_MmzFlush_End(data()),"DMA CPU sync end");}
  ~Memory(){if(value.u64VirAddr)RVE_DRM_MmzFree(data());}
};
struct Frame {
  Memory input, staging;
  IVE_IMAGE_S source{},images[IVE_PYRAMID_MAX_LEVEL]{};
  IVE_PYRAMID_CTRL_S control{};
  int width=0,height=0;
  void configure(int w,int h) {
    if(width==w && height==h)return;
    if((w&7)||(h&7))throw std::runtime_error("RKIVE probe requires image dimensions divisible by 8");
    width=w;height=h;
    unsigned stride=(w+15)&~15;
    input.allocate(stride*h);
    source={};source.u32Width=w;source.u32Height=h;source.enType=IVE_IMAGE_TYPE_U8C1;
    source.au32Stride[0]=stride;source.au64PhyAddr[0]=input.value.u64PhyAddr;
    source.au64VirAddr[0]=input.value.u64VirAddr;
    control={};control.level=3;
    int bytes=RK_MPI_IVE_Pyramid_GetSize(w,h,&control);
    if(bytes<=0)throw std::runtime_error("RKIVE Pyramid_GetSize failed");
    staging.allocate(bytes);control.stPyramidMem=staging.value;
  }
};
struct Device {
  Frame frames[2];int active=0;
  Memory previous_points,next_points,status,error;
  unsigned long pyramid_calls=0,flow_calls=0;
  Device() {
    check(RK_MPI_IVE_Init(),"RKIVE init");
    previous_points.allocate(500*sizeof(IVE_POINT_S25Q7_S));
    next_points.allocate(500*sizeof(IVE_POINT_S25Q7_S));
    status.allocate(512);error.allocate(1024);
  }
  ~Device(){std::fprintf(stderr,"RKIVE completed pyramid_calls=%lu flow_calls=%lu\n",pyramid_calls,flow_calls);RK_MPI_IVE_Deinit();}
  void wait(int rc,IVE_HANDLE handle) {
    check(rc,"RKIVE submit");bool done=false;
    check(RK_MPI_IVE_Query(handle,&done,true),"RKIVE query");
    if(!done)throw std::runtime_error("RKIVE query completed without finish");
  }
  Frame &build(const cv::Mat &image) {
    Frame &frame=frames[active];active^=1;
    frame.configure(image.cols,image.rows);
    frame.input.begin();
    for(int y=0;y<image.rows;y++)std::memcpy(static_cast<unsigned char*>(frame.input.data())+y*frame.source.au32Stride[0],image.ptr(y),image.cols);
    frame.input.end();
    IVE_HANDLE handle=-1;
    int rc=RK_MPI_IVE_Pyramid_Create(&handle,&frame.source,frame.images,&frame.control,true);
    wait(rc,handle);
    ++pyramid_calls;
    return frame;
  }
  // For hardware-LK-only, fill CMA pyramid images from OpenCV's image-only pyramid.
  void upload(Frame &frame,const std::vector<cv::Mat> &pyr) {
    frame.configure(pyr[0].cols,pyr[0].rows);
    frame.staging.begin();
    // SDK image descriptors are normally populated by Pyramid_Create. Set the
    // same contiguous layout ourselves, without running hardware pyramid work.
    size_t offset=0;
    for(int level=0;level<=3;level++) {
      auto &dst=frame.images[level];const auto &src=pyr.at(level);
      dst={};dst.enType=IVE_IMAGE_TYPE_U8C1;dst.u32Width=src.cols;dst.u32Height=src.rows;
      dst.au32Stride[0]=(src.cols+15)&~15;
      dst.au64PhyAddr[0]=frame.staging.value.u64PhyAddr+offset;
      dst.au64VirAddr[0]=frame.staging.value.u64VirAddr+offset;
      if(offset+dst.au32Stride[0]*dst.u32Height>frame.staging.value.u32Size)throw std::runtime_error("RKIVE pyramid upload overflow");
      auto *output=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(dst.au64VirAddr[0]));
      for(int y=0;y<src.rows;y++)std::memcpy(output+y*dst.au32Stride[0],src.ptr(y),src.cols);
      offset+=dst.au32Stride[0]*dst.u32Height;
    }
    frame.staging.end();
  }
  Frame &lookup(const std::vector<cv::Mat> &pyr) {
    for(auto &f:frames)if(f.images[0].au64VirAddr[0]==reinterpret_cast<uintptr_t>(pyr[0].data))return f;
    throw std::runtime_error("RKIVE pyramid lifetime mismatch");
  }
};
Device &device(){static Device value;return value;}
}

void pyramid(const cv::Mat &image,std::vector<cv::Mat> &result,cv::Size window,int levels) {
  Mode selected=mode();
  if(selected==Mode::Software){cv::buildOpticalFlowPyramid(image,result,window,levels);return;}
  if(selected==Mode::Software4){cv::buildOpticalFlowPyramid(image,result,window,3);return;}
  if(selected==Mode::Lk){cv::buildOpticalFlowPyramid(image,result,window,3,false);return;}
  if(window!=cv::Size(15,15))throw std::runtime_error("RKIVE fixed-window backend requires 15x15");
  auto &dev=device();auto &frame=dev.build(image);
  frame.staging.begin();result.clear();
  for(int level=0;level<=3;level++) {
    const auto &src=frame.images[level];
    cv::Mat view(src.u32Height,src.u32Width,CV_8UC1,reinterpret_cast<void*>(static_cast<uintptr_t>(src.au64VirAddr[0])),src.au32Stride[0]);
    if(selected==Mode::Both)result.push_back(view);
    else {
      // OpenCV requires padded pyramid ROIs and its own Scharr derivatives.
      std::vector<cv::Mat> padded;
      cv::buildOpticalFlowPyramid(view,padded,window,0,true);
      result.insert(result.end(),padded.begin(),padded.end());
    }
  }
  frame.staging.end();
}

void flow(const std::vector<cv::Mat> &previous,const std::vector<cv::Mat> &current,
          const std::vector<cv::Point2f> &points,std::vector<cv::Point2f> &next,
          std::vector<unsigned char> &status,std::vector<float> &error,
          cv::Size window,int levels,cv::TermCriteria criteria,int flags) {
  Mode selected=mode();
  if(selected==Mode::Software){cv::calcOpticalFlowPyrLK(previous,current,points,next,status,error,window,levels,criteria,flags);return;}
  if(selected==Mode::Software4 || selected==Mode::Pyramid){
    criteria.maxCount=20;
    cv::calcOpticalFlowPyrLK(previous,current,points,next,status,error,window,3,criteria,flags);return;
  }
  if(window!=cv::Size(15,15) || !(flags&cv::OPTFLOW_USE_INITIAL_FLOW))throw std::runtime_error("Unsupported RKIVE LK flags/window");
  Device &dev=device();
  Frame *prev,*curr;
  if(selected==Mode::Both){prev=&dev.lookup(previous);curr=&dev.lookup(current);}
  else {dev.upload(dev.frames[0],previous);dev.upload(dev.frames[1],current);prev=&dev.frames[0];curr=&dev.frames[1];}
  status.assign(points.size(),0);error.assign(points.size(),0);
  for(size_t offset=0;offset<points.size();offset+=500) {
    size_t count=std::min<size_t>(500,points.size()-offset);
    dev.previous_points.begin();dev.next_points.begin();
    auto *a=static_cast<IVE_POINT_S25Q7_S*>(dev.previous_points.data());
    auto *b=static_cast<IVE_POINT_S25Q7_S*>(dev.next_points.data());
    for(size_t i=0;i<count;i++) {
      a[i].s25q7X=std::lround(points[offset+i].x*128);a[i].s25q7Y=std::lround(points[offset+i].y*128);
      b[i].s25q7X=std::lround(next[offset+i].x*128);b[i].s25q7Y=std::lround(next[offset+i].y*128);
    }
    dev.previous_points.end();dev.next_points.end();
    IVE_LK_OPTICAL_FLOW_PYR_CTRL_S control{};
    control.enOutMode=IVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_BOTH;
    control.bUseInitFlow=RK_TRUE;control.u16PtsNum=count;control.u8MaxLevel=3;
    control.u8IterCnt=20;control.u0q8MinEigThr=1;control.u0q11Eps=1;
    IVE_HANDLE handle=-1;
    int rc=RK_MPI_IVE_LKOpticalFlowPyr(&handle,prev->images,curr->images,&dev.previous_points.value,&dev.next_points.value,&dev.status.value,&dev.error.value,&control,true);
    dev.wait(rc,handle);++dev.flow_calls;
    dev.next_points.begin();dev.status.begin();dev.error.begin();
    auto *valid=static_cast<unsigned char*>(dev.status.data());
    auto *errors=static_cast<IVE_U9Q7*>(dev.error.data());
    for(size_t i=0;i<count;i++) {
      next[offset+i]=cv::Point2f(b[i].s25q7X/128.f,b[i].s25q7Y/128.f);
      status[offset+i]=valid[i]?1:0;error[offset+i]=errors[i]/128.f;
    }
    dev.next_points.end();dev.status.end();dev.error.end();
  }
}
}

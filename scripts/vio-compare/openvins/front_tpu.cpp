// Pixel frontend transport: one input upload and one C906 command per stereo frame.
#include "front_tpu.hpp"
#include "tpu_lk.hpp"
#include "openvins_frontend_config.h"
#include "../probes/c906-ive/shared.h"
#include "cam/CamEqui.h"
#include "../probes/c906-frontend/front_intrinsics.h"
#include <mutex>
#include "../probes/c906-frontend/front_detail.h"
#if OPENVINS_FRONT_CPU_TAIL
#include "../probes/c906-frontend/front_tail.h"
#include "../probes/cpu-window-lk/window_lk.hpp"
#endif
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <opencv2/video/tracking.hpp>
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
#include "cvi_sys.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
extern "C" int lk_tpu_make_jobs(unsigned char*,uint64_t);
#endif
namespace ov_core {
namespace {
TpuFront::Stats totals;
front_detail last_detail{};
unsigned last_points[2]{},last_valid[2]{};
std::mutex intrinsics_mutex;
front_intrinsics_data latest_camera_intrinsics{};
bool flag(const char *key){const char *v=std::getenv(key);return v && std::strcmp(v,"0");}
bool emulate(){
#ifdef OPENVINS_TPU_LK_EMULATED_BUILD
 return true;
#else
 return flag("OPENVINS_TPU_EMULATE");
#endif
}
#if OPENVINS_FRONT_LOCK_EXEC && !defined(OPENVINS_TPU_LK_EMULATED_BUILD)
void lock_executable(){
 if(!flag("OPENVINS_TPU_LOCK_EXEC"))return;
 char executable[1024];ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
 if(n<0 || n>=ssize_t(sizeof(executable)-1))throw std::runtime_error("Cannot resolve executable for page locking");
 executable[n]=0;std::ifstream maps("/proc/self/maps");std::string line;size_t bytes=0;
 while(std::getline(maps,line)){
   unsigned long begin=0,end=0;char permissions[5]={};
   if(line.find(executable)==std::string::npos || std::sscanf(line.c_str(),"%lx-%lx %4s",&begin,&end,permissions)!=3)continue;
   if(end<=begin || mlock((void*)begin,end-begin))throw std::runtime_error("Cannot lock executable mapping");
   bytes+=end-begin;
 }
 if(!bytes)throw std::runtime_error("Executable mapping not found");
 std::cerr<<"FRONT_EXEC_LOCK bytes="<<bytes<<'\n';
}
#endif
struct Workspace {
  unsigned char *base=nullptr;unsigned width=0,height=0;
  unsigned ive_flags=0;
  std::vector<unsigned char> emulated;
  front_intrinsics_data last_intrinsics{};
#if OPENVINS_FRONT_CPU_TAIL
  std::unique_ptr<window_lk::Workspace> tail;
#endif
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
  CVI_U64 physical=0;int fd=-1;
  struct IveAllocation {
    CVI_U64 physical=0;void *memory=nullptr;
    ~IveAllocation(){if(memory)CVI_SYS_IonFree(physical,memory);}
  } ive;
#if OPENVINS_FRONT_IVE_HIST || OPENVINS_FRONT_IVE_ST
  void configure_ive(){
    if(!ive_flags)return;
    if(CVI_SYS_IonAlloc_Cached(&ive.physical,&ive.memory,"openvins_ive",IVE_ARENA_BYTES) || !ive.memory || ive.physical>0x8ff40000u-IVE_ARENA_BYTES)throw std::runtime_error("IVE ION allocation failed");
    std::memset(ive.memory,0,IVE_ARENA_BYTES);
    unsigned quality=16;
    if(const char *value=std::getenv("OPENVINS_TPU_IVE_QUALITY")){
      char *end=nullptr;unsigned long q=std::strtoul(value,&end,10);
      if(!*value || *end || !q || q>255)throw std::runtime_error("IVE quality must be 1..255");quality=q;
    }
    for(unsigned index=0;index<IVE_PROGRAMS;++index){
      bool hist=index<2;if(!(ive_flags&(hist?IVE_FLAG_HIST:IVE_FLAG_ST)))continue;
      unsigned stride=width,source=physical+FRONT_RAW_OFF+index*FRONT_PIXELS;
      if(!hist){lk_tpu_level levels[LK_TPU_LEVELS]{};
        front_image_layout(levels,FRONT_PYR_OFF+(index-2)*FRONT_CAM_BYTES,width,height);
        stride=levels[0].row_bytes;source=physical+levels[0].planes[0]+LK_TPU_BORDER*stride+LK_TPU_BORDER;
      }
      auto *program=(ive_program*)((unsigned char*)ive.memory+IVE_PROGRAM_OFF+index*IVE_PROGRAM_BYTES);
      if(ive_make_program(program,hist?0:1,width,height,stride,source,
          ive.physical+(hist?IVE_HIST_OFF+index*1024:IVE_ST_OUT_OFF),ive.physical+IVE_SCRATCH_OFF,quality))throw std::runtime_error("IVE register program generation failed");
    }
    auto &c=*(ive_control*)(base+IVE_META_OFF);c={};c.magic=IVE_MAGIC;c.version=IVE_VERSION;
    c.arena=ive.physical;c.arena_bytes=IVE_ARENA_BYTES;c.flags=ive_flags;c.quality=quality;c.width=width;c.height=height;c.stride=front_image_stride(width);
    if(CVI_SYS_IonFlushCache(ive.physical,ive.memory,IVE_ARENA_BYTES))throw std::runtime_error("IVE program clean failed");
    std::cerr<<"FRONT_IVE flags="<<ive_flags<<" quality="<<quality<<" stride="<<c.stride<<" direct_gray=1\n";
  }
#endif
#endif
  Workspace(unsigned w,unsigned h):width(w),height(h){
    static_assert(FRONT_META_OFF+sizeof(front_control)<FRONT_PYR_DMA_OFF,"Frontend metadata overlaps jobs");
    static_assert(FRONT_STATE_OFF+sizeof(front_state)<FRONT_WEIGHTS_OFF,"Frontend state overlaps weights");
    static_assert(FRONT_SNAPSHOT_OFF+sizeof(front_snapshot)<FRONT_INTRINSICS_OFF,"Snapshot overlaps calibration");
    static_assert(FRONT_INTRINSICS_OFF+sizeof(front_intrinsics_channel)<FRONT_PYR_OFF,"Calibration overlaps images");
    static_assert(IVE_META_OFF+sizeof(ive_control)<FRONT_PYR_OFF,"IVE metadata overlaps images");
    ive_flags=(flag("OPENVINS_TPU_IVE_HIST")?IVE_FLAG_HIST:0)|(flag("OPENVINS_TPU_IVE_ST")?IVE_FLAG_ST:0);
#if !OPENVINS_FRONT_IVE_HIST
    if(ive_flags&IVE_FLAG_HIST)throw std::runtime_error("IVE histogram was not compiled");
#endif
#if !OPENVINS_FRONT_IVE_ST
    if(ive_flags&IVE_FLAG_ST)throw std::runtime_error("IVE ST was not compiled");
#endif
    if(ive_flags && (!FRONT_ALIGNED_IMAGES || emulate()))throw std::runtime_error("IVE requires aligned images and real C906 hardware");
    if(emulate()){emulated.resize(FRONT_BYTES);base=emulated.data();return;}
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
#if OPENVINS_FRONT_LOCK_EXEC
    lock_executable();
#endif
    void *mapping=nullptr;
    if(CVI_SYS_IonAlloc_Cached(&physical,&mapping,"openvins_front",FRONT_BYTES) || !mapping || physical>0x8ff40000u-FRONT_BYTES){
      if(mapping)CVI_SYS_IonFree(physical,mapping);
      throw std::runtime_error("Frontend ION allocation failed");}
    base=(unsigned char*)mapping;std::memset(base,0,FRONT_BYTES);
    fd=open("/dev/c906-tpu-probe",O_RDWR);
    if(fd<0 || lk_tpu_make_jobs(base,physical) || front_make_jobs(base,physical,w,h)){
      if(fd>=0)close(fd);
      CVI_SYS_IonFree(physical,base);base=nullptr;
      throw std::runtime_error("Frontend command preparation failed");}
    try{
#if OPENVINS_FRONT_IVE_HIST || OPENVINS_FRONT_IVE_ST
      configure_ive();
#endif
      if(CVI_SYS_IonFlushCache(physical,base,FRONT_BYTES))throw std::runtime_error("Frontend command clean failed");
    }catch(...){close(fd);fd=-1;CVI_SYS_IonFree(physical,base);base=nullptr;throw;}
#if OPENVINS_FRONT_CPU_TAIL
    if(TpuFront::cpu_tail()){
      // Fault in the sparse-gradient/LK code and workspace before live jobs.
      // These private warm-up images never modify C906-owned pyramid slots.
      cv::Mat image(h,w,CV_8UC1);
      for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)image.ptr(y)[x]=(x*x+y*15+x*y)&255;
      std::vector<cv::Mat> dummy;cv::buildOpticalFlowPyramid(image,dummy,cv::Size(16,15),4,false);
      tail.reset(new window_lk::Workspace(dummy));
      std::vector<cv::Point2f> points,out;std::vector<unsigned char> valid;
      for(unsigned i=0;i<16;++i)points.emplace_back(w*.5f+i*.125f,h*.5f+i*.125f);
      tail->prepare(dummy,points);tail->track(dummy,points,out,valid);
    }
#endif
#endif
  }
  ~Workspace(){
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
    if(fd>=0)close(fd);
    if(base && !emulate())CVI_SYS_IonFree(physical,base);
#endif
  }
};
std::shared_ptr<Workspace> workspace;
void clean_intrinsics(void *base,unsigned offset,unsigned bytes){
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 if(!emulate() && CVI_SYS_IonFlushCache(workspace->physical+offset,(unsigned char*)base+offset,bytes))
  throw std::runtime_error("Intrinsics cache publication failed");
#endif
}
void publish_intrinsics(){
 if(workspace && latest_camera_intrinsics.words[0])
  front_intrinsics_publish(workspace->base,&latest_camera_intrinsics,clean_intrinsics);
}
probe_u64 stamp(double time){probe_u64 bits;std::memcpy(&bits,&time,sizeof(bits));return bits;}
probe_u64 clock_ticks(){return (probe_u64)(TpuLk::now_ms()*25000.0);}
std::vector<cv::Mat> pyramid(void *memory,const lk_tpu_level *p,unsigned n){
 std::vector<cv::Mat> out;for(unsigned l=0;l<n;++l){
  cv::Mat full(p[l].height+2*LK_TPU_BORDER,p[l].row_bytes,CV_8UC1,(unsigned char*)memory+p[l].planes[0]);
  out.push_back(full(cv::Rect(LK_TPU_BORDER,LK_TPU_BORDER,p[l].width,p[l].height)));}return out;
}
int cpu_flow(void *memory,const lk_tpu_level *a,const lk_tpu_level *b,unsigned levels,
    const front_point *points,unsigned count,lk_tpu_point *output,unsigned char *valid){
 std::vector<cv::Point2f> in,out;for(unsigned i=0;i<count;++i)in.emplace_back(points[i].x,points[i].y);out=in;
 std::vector<unsigned char> status;std::vector<float> error;
 cv::calcOpticalFlowPyrLK(pyramid(memory,a,levels),pyramid(memory,b,levels),in,out,status,error,cv::Size(16,15),5,
  cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
 for(unsigned i=0;i<count;++i){output[i]={out[i].x,out[i].y};valid[i]=status[i];}return 0;
}
int cpu_snapshot(void *memory,front_control *f){
 auto *s=(front_snapshot*)((unsigned char*)memory+FRONT_SNAPSHOT_OFF);
 const double begin=TpuLk::now_ms();
 while(!__atomic_load_n(&s->ready,__ATOMIC_ACQUIRE)){
   if(TpuLk::now_ms()-begin>500)return -56;
   std::this_thread::sleep_for(std::chrono::microseconds(20));
 }
 if(s->magic!=FRONT_MAGIC || s->timestamp!=f->timestamp || s->count[0]>FRONT_POINTS || s->count[1]>FRONT_POINTS)return -57;
 f->currid=s->currid;f->num_features=s->num_features;
 for(unsigned cam=0;cam<2;++cam){f->input_count[cam]=s->count[cam];
  std::memcpy(f->input[cam],s->points[cam],s->count[cam]*sizeof(front_point));}
 return 0;
}
}
bool TpuFront::enabled(){return TpuLk::enabled() && flag("OPENVINS_TPU_FRONTEND");}
bool TpuFront::early(){
#ifdef OPENVINS_C906_FRONTEND_EARLY
 const char *v=std::getenv("OPENVINS_TPU_FRONT_EARLY");
 return enabled() && TpuLk::pipeline() && (!v || std::strcmp(v,"0"));
#else
 return false;
#endif
}
bool TpuFront::latest_intrinsics(){
#if OPENVINS_FRONT_LATEST_INTRINSICS
 const char *v=std::getenv("OPENVINS_TPU_LATEST_INTRINSICS");
 return enabled() && (!v || std::strcmp(v,"0"));
#else
 return false;
#endif
}
bool TpuFront::cpu_tail(){
#if OPENVINS_FRONT_CPU_TAIL
 return enabled() && !emulate() && flag("OPENVINS_TPU_CPU_TAIL");
#else
 return false;
#endif
}
void TpuFront::assist_tail(const std::shared_ptr<TpuFrontRequest>& r,double timestamp){
#if OPENVINS_FRONT_CPU_TAIL && !defined(OPENVINS_TPU_LK_EMULATED_BUILD)
 if(!cpu_tail() || r->tail_jobs)return;
 auto w=std::static_pointer_cast<Workspace>(r->workspace);
 auto clean=[&](unsigned off,unsigned bytes){if(CVI_SYS_IonFlushCache(w->physical+off,w->base+off,bytes))throw std::runtime_error("CPU tail publication failed");};
 auto invalidate=[&](unsigned off,unsigned bytes){if(CVI_SYS_IonInvalidateCache(w->physical+off,w->base+off,bytes))throw std::runtime_error("CPU tail invalidation failed");};
 auto *host=FRONT_TAIL_PTR(front_tail_host,w->base,FRONT_TAIL_HOST_OFF);
 if(host->state==FRONT_TAIL_IDLE){host->state=FRONT_TAIL_AVAILABLE;clean(FRONT_TAIL_OFF,64);}
 invalidate(FRONT_TAIL_OFF+FRONT_TAIL_REMOTE_OFF,64);
 const auto *remote=FRONT_TAIL_PTR(front_tail_remote,w->base,FRONT_TAIL_REMOTE_OFF);
 if(remote->magic!=FRONT_TAIL_MAGIC || remote->timestamp!=stamp(timestamp) || remote->state!=FRONT_TAIL_OFFERED)return;
 const double begin=TpuLk::now_ms(),cpu=TpuLk::thread_cpu_ms();
 try{
   invalidate(FRONT_TAIL_OFF+FRONT_TAIL_TASK_OFF,sizeof(front_tail_task));
   const auto task=*FRONT_TAIL_PTR(front_tail_task,w->base,FRONT_TAIL_TASK_OFF);
   if(task.timestamp!=stamp(timestamp) || task.count!=16 || task.first+task.count>FRONT_POINTS || !task.levels || task.levels>LK_TPU_LEVELS)
     throw std::runtime_error("Invalid CPU tail descriptor");
   for(unsigned l=0;l<task.levels;++l)for(const auto *v:{&task.previous[l],&task.current[l]}){
     unsigned bytes=(v->height+36)*v->row_bytes;
     if(!lk_tpu_valid_stride(v->width,v->row_bytes) || v->width>640 || v->height>480 || v->planes[0]<FRONT_PYR_OFF || v->planes[0]>FRONT_BYTES-bytes)
       throw std::runtime_error("Invalid CPU tail pyramid");
     invalidate(v->planes[0],bytes);
   }
   r->tail_sync_ms=TpuLk::now_ms()-begin;
   auto previous=pyramid(w->base,task.previous,task.levels),current=pyramid(w->base,task.current,task.levels);
   std::vector<cv::Point2f> points,out;points.reserve(16);
   for(unsigned i=0;i<16;++i)points.emplace_back(task.input[i].x,task.input[i].y);
   std::vector<unsigned char> valid;
   w->tail->prepare(previous,points);w->tail->track(current,points,out,valid);
   auto *result=FRONT_TAIL_PTR(front_tail_result,w->base,FRONT_TAIL_RESULT_OFF);
   for(unsigned i=0;i<16;++i){result->output[i]={out[i].x,out[i].y};result->valid[i]=valid[i];}
   clean(FRONT_TAIL_OFF+FRONT_TAIL_RESULT_OFF,sizeof(*result));
   host->state=FRONT_TAIL_HOST_DONE;clean(FRONT_TAIL_OFF,64);
   r->tail_ms=TpuLk::now_ms()-begin;r->tail_cpu_ms=TpuLk::thread_cpu_ms()-cpu;r->tail_jobs=1;
   TpuLk::record("front_tail_cpu",timestamp,begin,begin+r->tail_ms,r->tail_cpu_ms);
 }catch(...){host->state=FRONT_TAIL_HOST_FAILED;clean(FRONT_TAIL_OFF,64);throw;}
#else
 (void)r;(void)timestamp;
#endif
}
void TpuFront::update_intrinsics(const std::array<std::shared_ptr<CamBase>,2>& cameras){
 if(!latest_intrinsics())return;
 std::lock_guard<std::mutex> lock(intrinsics_mutex);
 front_intrinsics_data data{};data.words[0]=latest_camera_intrinsics.words[0]+1;
 for(unsigned cam=0;cam<2;++cam){
  const auto values=cameras[cam]->get_value();
  if(values.size()!=8 || !values.allFinite())throw std::runtime_error("Invalid published camera intrinsics");
  data.words[1+cam*10]=dynamic_cast<CamEqui*>(cameras[cam].get())?2:1;
  data.words[2+cam*10]=probe_u64(cameras[cam]->w())|(probe_u64(cameras[cam]->h())<<32);
  for(unsigned j=0;j<8;++j){double value=values(j);std::memcpy(&data.words[3+cam*10+j],&value,8);}
 }
 latest_camera_intrinsics=data;publish_intrinsics();
}
TpuFront::Stats TpuFront::stats(){return totals;}
std::shared_ptr<TpuFrontRequest> TpuFront::prepare(const CameraData &message,double previous,
    const std::array<std::vector<cv::KeyPoint>,2>& points,const std::array<std::vector<size_t>,2>& ids,
    size_t currid,int features,int threshold,int gx,int gy,int minpx,int histogram,bool deferred){
 auto r=std::make_shared<TpuFrontRequest>();r->prepare_begin=TpuLk::now_ms();double cpu=TpuLk::thread_cpu_ms();
 if(message.images.size()!=2 || message.masks.size()!=2 || message.sensor_ids.size()!=2)throw std::runtime_error("C906 frontend requires stereo input");
 if(message.sensor_ids[0]!=0 || message.sensor_ids[1]!=1)
   throw std::runtime_error("C906 frontend requires ordered camera IDs 0, 1");
 unsigned width=message.images[0].cols,height=message.images[0].rows;
 if(width!=640 || (height!=400 && height!=480) || histogram<0 || histogram>1 || features>120 || features<=0 || gx<=0 || gy<=0 || gx>16 || gy>16 || gx*gy>features || minpx<4 || minpx>128)
   throw std::runtime_error("Unsupported C906 frontend resolution, histogram or grid settings");
 {
  std::lock_guard<std::mutex> lock(intrinsics_mutex);
  if(!workspace){workspace=std::make_shared<Workspace>(width,height);publish_intrinsics();}
 }
 if(workspace->width!=width || workspace->height!=height)throw std::runtime_error("Restart frontend before changing resolution");
 auto &w=*workspace;auto &f=*(front_control*)(w.base+FRONT_META_OFF);
 f.magic=FRONT_MAGIC;f.version=FRONT_VERSION;f.width=width;f.height=height;f.histogram=histogram;
 f.num_features=features;f.threshold=threshold;f.grid_x=gx;f.grid_y=gy;f.min_px_dist=minpx;
 f.currid=currid;f.timestamp=stamp(message.timestamp);f.previous_timestamp=stamp(previous);f.deadline_us=500000;
 f.deferred_snapshot=deferred;
#if OPENVINS_FRONT_CPU_TAIL
 std::memset(w.base+FRONT_TAIL_OFF,0,FRONT_TAIL_BYTES);
 if(cpu_tail()){
   auto *host=FRONT_TAIL_PTR(front_tail_host,w.base,FRONT_TAIL_HOST_OFF);
   host->magic=FRONT_TAIL_MAGIC;host->timestamp=f.timestamp;
 }
#endif
 auto *snapshot=(front_snapshot*)(w.base+FRONT_SNAPSHOT_OFF);
 if(deferred){__atomic_store_n(&snapshot->ready,0,__ATOMIC_RELEASE);}
 for(unsigned cam=0;cam<2;++cam){
   if(message.images[cam].type()!=CV_8UC1 || message.masks[cam].type()!=CV_8UC1 || message.images[cam].size()!=message.images[0].size() || message.masks[cam].size()!=message.images[0].size() || points[cam].size()!=ids[cam].size() || points[cam].size()>FRONT_POINTS)
      throw std::runtime_error("Invalid C906 frontend image or point buffer");
   f.input_count[cam]=points[cam].size();
   for(unsigned i=0;i<points[cam].size();++i)f.input[cam][i]={points[cam][i].pt.x,points[cam][i].pt.y,points[cam][i].response,0,ids[cam][i]};
   for(unsigned y=0;y<height;++y){std::memcpy(w.base+FRONT_RAW_OFF+cam*FRONT_PIXELS+y*width,message.images[cam].ptr(y),width);
      std::memcpy(w.base+FRONT_INPUT_MASK_OFF+cam*FRONT_PIXELS+y*width,message.masks[cam].ptr(y),width);}
 }
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 if(!emulate()){
  if(w.ive_flags){auto &c=*(ive_control*)(w.base+IVE_META_OFF);c.capabilities=0;c.status=0;c.histogram_calls=c.st_calls=0;c.histogram_ticks=c.st_ticks=c.pack_ticks=0;
    if(CVI_SYS_IonFlushCache(w.physical+IVE_META_OFF,&c,sizeof(c)))throw std::runtime_error("IVE metadata clean failed");}
  auto &ctl=*(tpu_probe_control*)w.base;ctl.magic=TPU_PROBE_MAGIC;ctl.version=TPU_PROBE_VERSION;ctl.operation=TPU_PROBE_FRONT;ctl.timeout_us=10000;ctl.completed=0;ctl.status=-99;
  if((deferred && CVI_SYS_IonFlushCache(w.physical+FRONT_SNAPSHOT_OFF,snapshot,sizeof(*snapshot))) ||
     CVI_SYS_IonFlushCache(w.physical,w.base,TPU_PROBE_CONTROL_BYTES) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_META_OFF,&f,sizeof(f)) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_RAW_OFF,w.base+FRONT_RAW_OFF,2*FRONT_PIXELS) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_INPUT_MASK_OFF,w.base+FRONT_INPUT_MASK_OFF,2*FRONT_PIXELS))throw std::runtime_error("Frontend input flush failed");
#if OPENVINS_FRONT_CPU_TAIL
  if(CVI_SYS_IonFlushCache(w.physical+FRONT_TAIL_OFF,w.base+FRONT_TAIL_OFF,FRONT_TAIL_BYTES))throw std::runtime_error("CPU tail reset flush failed");
#endif
 }
#endif
 r->workspace=workspace;r->prepare_end=TpuLk::now_ms();r->prepare_cpu=TpuLk::thread_cpu_ms()-cpu;return r;
}
void TpuFront::publish(const std::shared_ptr<TpuFrontRequest>& r,double timestamp,
    const std::array<std::vector<cv::KeyPoint>,2>& points,const std::array<std::vector<size_t>,2>& ids,
    size_t currid,int features){
 r->snapshot_begin=TpuLk::now_ms();const double cpu=TpuLk::thread_cpu_ms();
 auto w=std::static_pointer_cast<Workspace>(r->workspace);
 auto *s=(front_snapshot*)(w->base+FRONT_SNAPSHOT_OFF);
 s->magic=FRONT_MAGIC;s->timestamp=stamp(timestamp);s->currid=currid;s->num_features=features;
 for(unsigned cam=0;cam<2;++cam){
  if(points[cam].size()!=ids[cam].size() || points[cam].size()>FRONT_POINTS)throw std::runtime_error("Invalid late frontend snapshot");
  s->count[cam]=points[cam].size();
  for(unsigned i=0;i<points[cam].size();++i)s->points[cam][i]={points[cam][i].pt.x,points[cam][i].pt.y,points[cam][i].response,0,ids[cam][i]};
 }
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 if(!emulate() && CVI_SYS_IonFlushCache(w->physical+FRONT_SNAPSHOT_OFF,s,sizeof(*s)))throw std::runtime_error("Frontend snapshot flush failed");
#endif
 __atomic_store_n(&s->ready,1,__ATOMIC_RELEASE);
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 if(!emulate() && CVI_SYS_IonFlushCache(w->physical+FRONT_SNAPSHOT_OFF,s,64))throw std::runtime_error("Frontend ready flush failed");
#endif
 r->snapshot_end=TpuLk::now_ms();r->snapshot_cpu=TpuLk::thread_cpu_ms()-cpu;
}
void TpuFront::execute(const std::shared_ptr<TpuFrontRequest>& r){
 auto w=std::static_pointer_cast<Workspace>(r->workspace);r->begin=TpuLk::now_ms();double cpu=TpuLk::thread_cpu_ms();int ret=0;
 if(emulate()){front_ops ops={clock_ticks,front_cpu_pyramid,cpu_flow,nullptr,cpu_snapshot};ret=front_run(w->base,&ops);
  if(latest_intrinsics()){
   front_intrinsics_read(w->base,&w->last_intrinsics,nullptr);
   front_detail_at(w->base)->intrinsics_version=w->last_intrinsics.words[0];
  }
 }
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 else{
   uint32_t address=w->physical;
   ret=w->ive_flags?ioctl(w->fd,_IOW('T',6,uint32_t),&address):ioctl(w->fd,_IOW('T',4,uint32_t),&address);
   if(ret || CVI_SYS_IonInvalidateCache(w->physical,w->base,TPU_PROBE_CONTROL_BYTES) ||
      CVI_SYS_IonInvalidateCache(w->physical+FRONT_META_OFF,w->base+FRONT_META_OFF,sizeof(front_control)))throw std::runtime_error("Frontend mailbox/cache failure");
   auto &ctl=*(tpu_probe_control*)w->base;ret=ctl.status;if(!ret && ctl.completed!=1)ret=-99;
   if(w->ive_flags && !ret){auto &c=*(ive_control*)(w->base+IVE_META_OFF);
     if(CVI_SYS_IonInvalidateCache(w->physical+IVE_META_OFF,&c,sizeof(c)) || (c.capabilities&w->ive_flags)!=w->ive_flags || c.status || c.pack_ticks)throw std::runtime_error("C906 IVE capability/status mismatch");
     r->ive_histogram_calls=c.histogram_calls;r->ive_st_calls=c.st_calls;r->ive_histogram_ms=c.histogram_ticks/25000.;r->ive_st_ms=c.st_ticks/25000.;
   }
 }
#endif
 r->end=TpuLk::now_ms();r->wait_cpu=TpuLk::thread_cpu_ms()-cpu;
 if(ret)throw std::runtime_error("C906 frontend failed: "+std::to_string(ret));
#if OPENVINS_FRONT_CPU_TAIL && !defined(OPENVINS_TPU_LK_EMULATED_BUILD)
 if(cpu_tail()){
   if(CVI_SYS_IonInvalidateCache(w->physical+FRONT_TAIL_OFF+FRONT_TAIL_REMOTE_OFF,w->base+FRONT_TAIL_OFF+FRONT_TAIL_REMOTE_OFF,64))
     throw std::runtime_error("CPU tail completion invalidate failed");
   const auto *remote=FRONT_TAIL_PTR(front_tail_remote,w->base,FRONT_TAIL_REMOTE_OFF);
   if(remote->magic!=FRONT_TAIL_MAGIC || remote->state!=FRONT_TAIL_FINISHED || remote->timestamp!=((front_control*)(w->base+FRONT_META_OFF))->timestamp)
     throw std::runtime_error("C906 firmware does not acknowledge CPU tail protocol");
   r->tail_remote_wait_ms=remote->wait_ticks/25000.;
 }
#endif
 r->result=*(front_control*)(w->base+FRONT_META_OFF);
 const auto &f=r->result;
 if(f.magic!=FRONT_MAGIC || f.version!=FRONT_VERSION || f.width!=w->width || f.height!=w->height ||
    f.count[0]>FRONT_POINTS || f.count[1]>FRONT_POINTS)
   throw std::runtime_error("Invalid C906 frontend result metadata");
}
void TpuFront::accept(const std::shared_ptr<TpuFrontRequest>& r,double timestamp){
 last_detail=*front_detail_at(std::static_pointer_cast<Workspace>(r->workspace)->base);
 if(last_detail.magic!=FRONT_DETAIL_MAGIC)last_detail={};
 ++totals.calls;totals.prepare_ms+=r->prepare_end-r->prepare_begin;totals.prepare_cpu_ms+=r->prepare_cpu;
 totals.tail_jobs+=r->tail_jobs;totals.tail_ms+=r->tail_ms;totals.tail_cpu_ms+=r->tail_cpu_ms;
 totals.tail_sync_ms+=r->tail_sync_ms;totals.tail_remote_wait_ms+=r->tail_remote_wait_ms;
 totals.ive_histogram_calls+=r->ive_histogram_calls;totals.ive_st_calls+=r->ive_st_calls;
 totals.ive_histogram_ms+=r->ive_histogram_ms;totals.ive_st_ms+=r->ive_st_ms;
 totals.remote_ms+=r->end-r->begin;totals.wait_cpu_ms+=r->wait_cpu;
 totals.snapshot_cpu_ms+=r->snapshot_cpu;
 totals.snapshot_wait_ms+=r->result.snapshot_wait_ticks/25000.0;
 const auto &f=r->result;totals.equalize_ms+=f.equalize_ticks/25000.0;totals.pyramid_ms+=f.pyramid_ticks/25000.0;
 for(unsigned cam=0;cam<2;++cam){last_points[cam]=f.count[cam];last_valid[cam]=0;for(unsigned i=0;i<f.count[cam];++i)last_valid[cam]+=f.valid[cam][i]!=0;}
 totals.detect_ms+=f.detect_ticks/25000.0;totals.lk_ms+=f.lk_ticks/25000.0;totals.total_ms+=f.total_ticks/25000.0;
 TpuLk::record("front_upload",timestamp,r->prepare_begin,r->prepare_end,r->prepare_cpu);
 TpuLk::record("front_request",timestamp,r->begin,r->end,r->wait_cpu);
 if(r->snapshot_end)TpuLk::record("front_snapshot",timestamp,r->snapshot_begin,r->snapshot_end,r->snapshot_cpu);
}
#define FRONT_FIELDS(X) X(calls) X(prepare_ms) X(prepare_cpu_ms) X(remote_ms) X(wait_cpu_ms) X(equalize_ms) X(pyramid_ms) X(detect_ms) X(lk_ms) X(total_ms) X(snapshot_cpu_ms) X(snapshot_wait_ms) X(tail_jobs) X(tail_ms) X(tail_cpu_ms) X(tail_sync_ms) X(tail_remote_wait_ms) X(ive_histogram_calls) X(ive_st_calls) X(ive_histogram_ms) X(ive_st_ms)
std::string TpuFront::diagnostic_header(){std::string out;
#define FIELD(n) out+=",front_" #n;
 FRONT_FIELDS(FIELD)
#undef FIELD
 out+=",front_left_points,front_right_points,front_left_valid,front_right_valid";
 out+=",detail_flags";
#define FIELD(n) out+=",detail_" #n "_ms";
 FRONT_DETAIL_TIMES(FIELD)
#undef FIELD
#define FIELD(n) out+=",detail_" #n;
 FRONT_DETAIL_COUNTS(FIELD)
#undef FIELD
 return out;
}
std::string TpuFront::diagnostic_delta(const Stats &old){std::ostringstream out;out<<std::setprecision(15);
#define FIELD(n) out<<','<<totals.n-old.n;
 FRONT_FIELDS(FIELD)
#undef FIELD
 out<<','<<(totals.calls>old.calls?last_points[0]:0)<<','<<(totals.calls>old.calls?last_points[1]:0)
    <<','<<(totals.calls>old.calls?last_valid[0]:0)<<','<<(totals.calls>old.calls?last_valid[1]:0);
 const auto d=totals.calls>old.calls ? last_detail : front_detail{};
 out<<','<<d.flags;
#define FIELD(n) out<<','<<d.n##_ticks/25000.0;
 FRONT_DETAIL_TIMES(FIELD)
#undef FIELD
#define FIELD(n) out<<','<<d.n;
 FRONT_DETAIL_COUNTS(FIELD)
#undef FIELD
 return out.str();
}
#undef FRONT_FIELDS
}

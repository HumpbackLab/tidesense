// Pixel frontend transport: one input upload and one C906 command per stereo frame.
#include "front_tpu.hpp"
#include "tpu_lk.hpp"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <opencv2/video/tracking.hpp>
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
#include "cvi_sys.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
extern "C" int lk_tpu_make_jobs(unsigned char*,uint64_t);
#endif
namespace ov_core {
namespace {
TpuFront::Stats totals;
bool flag(const char *key){const char *v=std::getenv(key);return v && std::strcmp(v,"0");}
bool emulate(){
#ifdef OPENVINS_TPU_LK_EMULATED_BUILD
 return true;
#else
 return flag("OPENVINS_TPU_EMULATE");
#endif
}
struct Workspace {
  unsigned char *base=nullptr;unsigned width=0,height=0;
  std::vector<unsigned char> emulated;
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
  CVI_U64 physical=0;int fd=-1;
#endif
  Workspace(unsigned w,unsigned h):width(w),height(h){
    static_assert(FRONT_META_OFF+sizeof(front_control)<FRONT_PYR_DMA_OFF,"Frontend metadata overlaps jobs");
    static_assert(FRONT_STATE_OFF+sizeof(front_state)<FRONT_WEIGHTS_OFF,"Frontend state overlaps weights");
    static_assert(FRONT_SNAPSHOT_OFF+sizeof(front_snapshot)<FRONT_PYR_OFF,"Snapshot overlaps image buffers");
    if(emulate()){emulated.resize(FRONT_BYTES);base=emulated.data();return;}
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
    void *mapping=nullptr;
    if(CVI_SYS_IonAlloc_Cached(&physical,&mapping,"openvins_front",FRONT_BYTES) || !mapping || physical>0x8ff40000u-FRONT_BYTES){
      if(mapping)CVI_SYS_IonFree(physical,mapping);
      throw std::runtime_error("Frontend ION allocation failed");}
    base=(unsigned char*)mapping;std::memset(base,0,FRONT_BYTES);
    fd=open("/dev/c906-tpu-probe",O_RDWR);
    if(fd<0 || lk_tpu_make_jobs(base,physical) || front_make_jobs(base,physical,w,h) || CVI_SYS_IonFlushCache(physical,base,FRONT_BYTES)){
      if(fd>=0)close(fd);
      CVI_SYS_IonFree(physical,base);base=nullptr;
      throw std::runtime_error("Frontend command preparation failed");}
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
 if(!workspace)workspace=std::make_shared<Workspace>(width,height);
 if(workspace->width!=width || workspace->height!=height)throw std::runtime_error("Restart frontend before changing resolution");
 auto &w=*workspace;auto &f=*(front_control*)(w.base+FRONT_META_OFF);
 f.magic=FRONT_MAGIC;f.version=FRONT_VERSION;f.width=width;f.height=height;f.histogram=histogram;
 f.num_features=features;f.threshold=threshold;f.grid_x=gx;f.grid_y=gy;f.min_px_dist=minpx;
 f.currid=currid;f.timestamp=stamp(message.timestamp);f.previous_timestamp=stamp(previous);f.deadline_us=500000;
 f.deferred_snapshot=deferred;
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
  auto &ctl=*(tpu_probe_control*)w.base;ctl.magic=TPU_PROBE_MAGIC;ctl.version=TPU_PROBE_VERSION;ctl.operation=TPU_PROBE_FRONT;ctl.timeout_us=10000;ctl.completed=0;ctl.status=-99;
  if((deferred && CVI_SYS_IonFlushCache(w.physical+FRONT_SNAPSHOT_OFF,snapshot,sizeof(*snapshot))) ||
     CVI_SYS_IonFlushCache(w.physical,w.base,TPU_PROBE_CONTROL_BYTES) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_META_OFF,&f,sizeof(f)) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_RAW_OFF,w.base+FRONT_RAW_OFF,2*FRONT_PIXELS) ||
     CVI_SYS_IonFlushCache(w.physical+FRONT_INPUT_MASK_OFF,w.base+FRONT_INPUT_MASK_OFF,2*FRONT_PIXELS))throw std::runtime_error("Frontend input flush failed");
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
 if(emulate()){front_ops ops={clock_ticks,front_cpu_pyramid,cpu_flow,nullptr,cpu_snapshot};ret=front_run(w->base,&ops);}
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
 else{
   uint32_t address=w->physical;ret=ioctl(w->fd,_IOW('T',4,uint32_t),&address);
   if(ret || CVI_SYS_IonInvalidateCache(w->physical,w->base,TPU_PROBE_CONTROL_BYTES) ||
      CVI_SYS_IonInvalidateCache(w->physical+FRONT_META_OFF,w->base+FRONT_META_OFF,sizeof(front_control)))throw std::runtime_error("Frontend mailbox/cache failure");
   auto &ctl=*(tpu_probe_control*)w->base;ret=ctl.status;if(!ret && ctl.completed!=1)ret=-99;
 }
#endif
 r->end=TpuLk::now_ms();r->wait_cpu=TpuLk::thread_cpu_ms()-cpu;
 if(ret)throw std::runtime_error("C906 frontend failed: "+std::to_string(ret));
 r->result=*(front_control*)(w->base+FRONT_META_OFF);
 const auto &f=r->result;
 if(f.magic!=FRONT_MAGIC || f.version!=FRONT_VERSION || f.width!=w->width || f.height!=w->height ||
    f.count[0]>FRONT_POINTS || f.count[1]>FRONT_POINTS)
   throw std::runtime_error("Invalid C906 frontend result metadata");
}
void TpuFront::accept(const std::shared_ptr<TpuFrontRequest>& r,double timestamp){
 ++totals.calls;totals.prepare_ms+=r->prepare_end-r->prepare_begin;totals.prepare_cpu_ms+=r->prepare_cpu;
 totals.remote_ms+=r->end-r->begin;totals.wait_cpu_ms+=r->wait_cpu;
 totals.snapshot_cpu_ms+=r->snapshot_cpu;
 totals.snapshot_wait_ms+=r->result.snapshot_wait_ticks/25000.0;
 const auto &f=r->result;totals.equalize_ms+=f.equalize_ticks/25000.0;totals.pyramid_ms+=f.pyramid_ticks/25000.0;
 totals.detect_ms+=f.detect_ticks/25000.0;totals.lk_ms+=f.lk_ticks/25000.0;totals.total_ms+=f.total_ticks/25000.0;
 TpuLk::record("front_upload",timestamp,r->prepare_begin,r->prepare_end,r->prepare_cpu);
 TpuLk::record("front_request",timestamp,r->begin,r->end,r->wait_cpu);
 if(r->snapshot_end)TpuLk::record("front_snapshot",timestamp,r->snapshot_begin,r->snapshot_end,r->snapshot_cpu);
}
#define FRONT_FIELDS(X) X(calls) X(prepare_ms) X(prepare_cpu_ms) X(remote_ms) X(wait_cpu_ms) X(equalize_ms) X(pyramid_ms) X(detect_ms) X(lk_ms) X(total_ms) X(snapshot_cpu_ms) X(snapshot_wait_ms)
std::string TpuFront::diagnostic_header(){std::string out;
#define FIELD(n) out+=",front_" #n;
 FRONT_FIELDS(FIELD)
#undef FIELD
 return out;
}
std::string TpuFront::diagnostic_delta(const Stats &old){std::ostringstream out;out<<std::setprecision(15);
#define FIELD(n) out<<','<<totals.n-old.n;
 FRONT_FIELDS(FIELD)
#undef FIELD
 return out.str();
}
#undef FRONT_FIELDS
}

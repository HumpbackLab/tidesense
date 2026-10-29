#include "tpu_lk.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <time.h>
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "cvi_sys.h"
#include "lk_shared.h"
extern "C" int lk_tpu_make_jobs(uint8_t*,uint64_t);
#endif

namespace ov_core {
namespace {
TpuLk::Stats totals;
thread_local std::vector<std::shared_ptr<TpuLk::Request>> *deferred=nullptr;
std::vector<std::shared_ptr<TpuLk::Request>> cache;
thread_local std::promise<void> *submission=nullptr;
void mark_started(const std::shared_ptr<TpuLk::Request>& r) {
  r->started.store(true);
  if(submission){submission->set_value();submission=nullptr;}
}
bool flag(const char *name) {const char *v=std::getenv(name);return v && std::strcmp(v,"0");}
bool emulate() {
#ifdef OPENVINS_TPU_LK_EMULATED_BUILD
  return true;
#else
  return flag("OPENVINS_TPU_EMULATE");
#endif
}
double cpu_ms() {timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
std::vector<cv::Mat> image_levels(const std::vector<cv::Mat>& p) {
  std::vector<cv::Mat> out;for(const auto &m:p)if(m.type()==CV_8UC1)out.push_back(m);return out;
}
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
struct Workspace {
  CVI_U64 phys=0;void *data=nullptr;
  Workspace() {
    static_assert(LK_TPU_META_OFF+sizeof(lk_tpu_control)<=TPU_PROBE_CONTROL_BYTES,"LK telemetry overlaps DMA jobs");
    static_assert(LK_TPU_TRACE_OFF+LK_TPU_TRACE_MAX*sizeof(lk_tpu_trace_span)<=LK_TPU_PLANES_OFF,"LK trace overlaps images");
    if(CVI_SYS_IonAlloc_Cached(&phys,&data,"openvins_lk",LK_TPU_BYTES) || !data || phys>0x8ff40000u-LK_TPU_BYTES) {
      if(data)CVI_SYS_IonFree(phys,data);
      throw std::runtime_error("OpenVINS TPU ION allocation failed");
    }
    std::memset(data,0,LK_TPU_PLANES_OFF);
    if(lk_tpu_make_jobs(static_cast<uint8_t*>(data),phys)) {
      CVI_SYS_IonFree(phys,data);throw std::runtime_error("OpenVINS TPU job generation failed");
    }
  }
  ~Workspace(){if(data)CVI_SYS_IonFree(phys,data);}
};
std::vector<std::shared_ptr<Workspace>> pool;
std::mutex device_mutex;
int bridge() {
  static int fd=[](){int f=open("/dev/c906-tpu-probe",O_RDWR);if(f<0)throw std::runtime_error("OpenVINS: load C906 LK firmware and exclusive TPU bridge first");return f;}();
  return fd;
}
struct ClockSync { double offset=0,error=0; unsigned requests=0; bool valid=false; } clock_sync;
void sync_clock(Workspace &w) {
  auto &ctl=*static_cast<tpu_probe_control*>(w.data);
  double best=std::numeric_limits<double>::infinity(),offset=0;
  for(unsigned i=0;i<8;++i) {
    ctl.operation=TPU_PROBE_CLOCK;ctl.completed=0;ctl.status=-1;
    if(CVI_SYS_IonFlushCache(w.phys,w.data,TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("Clock probe flush failed");
    uint32_t address=w.phys;double begin=TpuLk::now_ms();
    int ret=ioctl(bridge(),_IOW('T',1,uint32_t),&address);
    const double end=TpuLk::now_ms();
    if(ret || CVI_SYS_IonInvalidateCache(w.phys,w.data,TPU_PROBE_CONTROL_BYTES) || ctl.status || ctl.completed!=1)
      throw std::runtime_error("LK timing needs C906 firmware with clock probe support");
    if(end-begin<best){best=end-begin;offset=(begin+end)*.5-ctl.run_ticks[0]/25000.0;}
  }
  clock_sync.offset=offset;clock_sync.error=best*.5;clock_sync.valid=true;
  ctl.operation=TPU_PROBE_LK;
  std::fprintf(stderr,"C906_CLOCK offset_ms=%.9f uncertainty_ms=%.6f requests=%u\n",offset,clock_sync.error,clock_sync.requests);
}
void pack(Workspace &w,const std::vector<cv::Mat>& a,const std::vector<cv::Mat>& b) {
  auto *base=static_cast<uint8_t*>(w.data);auto &m=*reinterpret_cast<lk_tpu_control*>(base+LK_TPU_META_OFF);
  m.trace_flags=flag("OPENVINS_TPU_TIMING")?LK_TPU_TRACE_MAGIC:0;m.trace_magic=0;
  m.version=LK_TPU_VERSION;m.batch=LK_TPU_BATCH;m.max_iterations=30;
  m.levels=std::min(a.size(),b.size());
  if(!m.levels || m.levels>LK_TPU_LEVELS)throw std::runtime_error("Unsupported TPU LK pyramid depth");
  unsigned off=LK_TPU_PLANES_OFF;
  for(unsigned l=0;l<m.levels;++l) {
    auto &v=m.level[l];v.width=a[l].cols;v.height=a[l].rows;v.row_bytes=v.width+2*LK_TPU_BORDER;
    if(a[l].size()!=b[l].size() || v.width>640 || v.height>480)throw std::runtime_error("Unsupported TPU LK image size");
    for(unsigned p=0;p<2;++p) {
      const cv::Mat &src=p?b[l]:a[l];cv::Mat border;
      cv::copyMakeBorder(src,border,LK_TPU_BORDER,LK_TPU_BORDER,LK_TPU_BORDER,LK_TPU_BORDER,cv::BORDER_REFLECT_101|cv::BORDER_ISOLATED);
      off=(off+63)&~63u;unsigned bytes=border.rows*v.row_bytes;
      if(off+bytes>LK_TPU_BYTES)throw std::runtime_error("TPU LK buffer overflow");
      v.planes[p]=off;
      for(int y=0;y<border.rows;++y)std::memcpy(base+off+y*v.row_bytes,border.ptr(y),v.row_bytes);
      off+=bytes;
    }
  }
  auto &c=*static_cast<tpu_probe_control*>(w.data);
  c.magic=TPU_PROBE_MAGIC;c.version=TPU_PROBE_VERSION;c.operation=TPU_PROBE_LK;c.timeout_us=10000;
  if(CVI_SYS_IonFlushCache(w.phys,w.data,LK_TPU_BYTES))throw std::runtime_error("TPU LK input cache flush failed");
}
#endif
void account(const std::shared_ptr<TpuLk::Request>& r) {
  ++totals.calls;totals.points+=r->input.size();totals.prepare_ms+=r->prepare_ms;
  totals.remote_ms+=r->remote_ms;totals.wait_cpu_ms+=r->wait_cpu_ms;
}
}
TpuLk::Deferred::Deferred(std::vector<std::shared_ptr<Request>>& requests, std::promise<void>* submitted) {
  if(deferred)throw std::runtime_error("Nested TPU worker context");
  deferred=&requests;submission=submitted;
}
TpuLk::Deferred::~Deferred(){
  if(submission){submission->set_value();submission=nullptr;}
  deferred=nullptr;
}
bool TpuLk::enabled(){return flag("OPENVINS_TPU_LK");}
bool TpuLk::pipeline(){const char *v=std::getenv("OPENVINS_TPU_PIPELINE");return enabled() && (!v || std::strcmp(v,"0"));}
double TpuLk::now_ms(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
TpuLk::Stats TpuLk::stats(){return totals;}
void TpuLk::clear_cache(){cache.clear();}
double TpuLk::thread_cpu_ms(){return cpu_ms();}
bool TpuLk::kickoff_wait(){return flag("OPENVINS_TPU_KICKOFF_WAIT");}
void TpuLk::record(const char *kind,double stamp,double begin,double end,double cpu,double uncertainty) {
  const double duration=end-begin;
  const std::string name=kind;
  if(name=="kickoff_wait")totals.kickoff_wait_ms+=duration;
  else if(name=="consume_wait")totals.consume_wait_ms+=duration;
  else if(name=="backend"){totals.backend_wall_ms+=duration;totals.backend_cpu_ms+=cpu;}
  else if(name=="worker"){totals.worker_wall_ms+=duration;totals.worker_cpu_ms+=cpu;}
  else if(name=="preprocess")totals.preprocess_ms+=duration;
  else if(name=="detection")totals.detection_ms+=duration;
  static std::ofstream trace=[](){
    std::ofstream out;const char *path=std::getenv("OPENVINS_TPU_TRACE");
    if(path){out.open(path);if(!out)throw std::runtime_error("Cannot open LK timing trace");}
    return out;
  }();
  if(!trace.is_open())return;
  static bool header=false;
  if(!header){trace<<"kind,frame_stamp,begin_ms,end_ms,cpu_ms,uncertainty_ms\n"<<std::setprecision(15);header=true;}
  trace<<kind<<','<<stamp<<','<<begin<<','<<end<<','<<cpu<<','<<uncertainty<<'\n';
  if(!trace)throw std::runtime_error("LK timing trace write failed");
}
#define TIMING_FIELDS(X) X(kickoff_wait_ms) X(consume_wait_ms) X(backend_wall_ms) X(backend_cpu_ms) \
 X(worker_wall_ms) X(worker_cpu_ms) X(preprocess_ms) X(detection_ms) X(c906_ms) X(tpu_device_ms) \
 X(c906_backend_overlap_ms) X(tpu_backend_overlap_ms) X(consume_c906_ms) X(consume_tpu_ms)
std::string TpuLk::diagnostic_header(){
  std::string out;
#define FIELD_HEADER(n) out+=",tpu_" #n;
  TIMING_FIELDS(FIELD_HEADER)
#undef FIELD_HEADER
  return out;
}
std::string TpuLk::diagnostic_delta(const Stats& previous){
  std::ostringstream out;out<<std::setprecision(15);
#define FIELD_DELTA(n) out<<','<<totals.n-previous.n;
  TIMING_FIELDS(FIELD_DELTA)
#undef FIELD_DELTA
  return out.str();
}
#undef TIMING_FIELDS

std::shared_ptr<TpuLk::Request> TpuLk::prepare(const std::vector<cv::Mat>& previous,
    const std::vector<cv::Mat>& next,const std::vector<cv::Point2f>& input,const std::vector<cv::Point2f>& initial,
    std::shared_ptr<void> reuse) {
  auto r=std::make_shared<Request>();r->previous=previous;r->next=next;r->input=input;r->initial=initial;
  if(input.size()!=initial.size())throw std::runtime_error("TPU LK point size mismatch");
  r->output.resize(input.size());r->status.resize(input.size());
  const double t=now_ms();
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
  if(!emulate() && !input.empty()) {
    if(reuse)r->workspace=reuse;
    else {
      std::shared_ptr<Workspace> w;
      for(auto &candidate:pool)if(candidate.use_count()==1){w=candidate;break;}
      if(!w){w=std::make_shared<Workspace>();pool.push_back(w);}
      pack(*w,image_levels(previous),image_levels(next));r->workspace=w;
    }
  }
#endif
  r->prepare_ms=now_ms()-t;return r;
}
void TpuLk::execute(const std::shared_ptr<Request>& r) {
  if(r->input.empty()){mark_started(r);return;}
  if(emulate()) {
    mark_started(r);r->output=r->initial;std::vector<float> error;double t=now_ms(),c=cpu_ms();
    cv::calcOpticalFlowPyrLK(r->previous,r->next,r->input,r->output,r->status,error,cv::Size(16,15),5,
        cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
    r->spans.emplace_back(t,now_ms());r->remote_ms=now_ms()-t;r->wait_cpu_ms=cpu_ms()-c;return;
  }
#ifndef OPENVINS_TPU_LK_EMULATED_BUILD
  auto w=std::static_pointer_cast<Workspace>(r->workspace);
  auto &m=*reinterpret_cast<lk_tpu_control*>(static_cast<uint8_t*>(w->data)+LK_TPU_META_OFF);
  std::lock_guard<std::mutex> lock(device_mutex);
  if(flag("OPENVINS_TPU_TIMING") && (!clock_sync.valid || clock_sync.requests%200==0))sync_clock(*w);
  for(size_t first=0;first<r->input.size();first+=LK_TPU_POINTS) {
    ++clock_sync.requests;m.trace_magic=0;
    m.count=std::min(size_t(LK_TPU_POINTS),r->input.size()-first);
    for(unsigned i=0;i<m.count;++i){m.input[i]={r->input[first+i].x,r->input[first+i].y};m.initial[i]={r->initial[first+i].x,r->initial[first+i].y};}
    if(CVI_SYS_IonFlushCache(w->phys,w->data,TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("TPU LK control flush failed");
    uint32_t address=w->phys;double t=now_ms(),c=cpu_ms();mark_started(r);
    int ret=ioctl(bridge(),_IOW('T',2,uint32_t),&address);
    r->wait_cpu_ms+=cpu_ms()-c;const double end=now_ms();r->spans.emplace_back(t,end);r->remote_ms+=end-t;
    if(ret || CVI_SYS_IonInvalidateCache(w->phys,w->data,TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("TPU LK mailbox/cache failure");
    auto &ctl=*static_cast<tpu_probe_control*>(w->data);
    if(ctl.status || ctl.completed!=1)throw std::runtime_error("TPU LK firmware failure: "+std::to_string(ctl.status));
    if(flag("OPENVINS_TPU_TIMING")) {
      if(m.trace_magic!=LK_TPU_TRACE_MAGIC || m.trace_overflow || m.trace_count>LK_TPU_TRACE_MAX)
        throw std::runtime_error("Incomplete C906 hardware timing trace");
      if(m.trace_count && CVI_SYS_IonInvalidateCache(w->phys+LK_TPU_TRACE_OFF,
          static_cast<uint8_t*>(w->data)+LK_TPU_TRACE_OFF,m.trace_count*sizeof(lk_tpu_trace_span)))
        throw std::runtime_error("TPU timing cache invalidate failed");
      r->clock_uncertainty_ms=clock_sync.error;
      r->c906_spans.emplace_back(m.trace_begin/25000.0+clock_sync.offset,m.trace_end/25000.0+clock_sync.offset);
      auto *spans=reinterpret_cast<lk_tpu_trace_span*>(static_cast<uint8_t*>(w->data)+LK_TPU_TRACE_OFF);
      for(unsigned i=0;i<m.trace_count;++i)r->tpu_spans.emplace_back(
          spans[i].begin/25000.0+clock_sync.offset,spans[i].end/25000.0+clock_sync.offset);
    }
    for(unsigned i=0;i<m.count;++i){r->output[first+i]={m.output[i].x,m.output[i].y};r->status[first+i]=m.valid[i];}
  }
#endif
}
void TpuLk::accept(const std::shared_ptr<Request>& r,double begin,double end,double stamp,double consume_begin,double consume_end) {
  account(r);totals.prefetched+=r->input.size();
  for(auto s:r->spans) {
    totals.overlap_ms+=std::max(0.0,std::min(end,s.second)-std::max(begin,s.first));
    record("host_request",stamp,s.first,s.second);
  }
  const auto overlap=[](double a,double b,std::pair<double,double> s){return std::max(0.0,std::min(b,s.second)-std::max(a,s.first));};
  static double last_stamp=-1;static unsigned frame_index=0;
  if(stamp!=last_stamp){last_stamp=stamp;++frame_index;}
  for(auto s:r->c906_spans){
    totals.c906_ms+=s.second-s.first;totals.c906_backend_overlap_ms+=overlap(begin,end,s);
    totals.consume_c906_ms+=overlap(consume_begin,consume_end,s);
    record("c906",stamp,s.first,s.second,0,r->clock_uncertainty_ms);
  }
  for(auto s:r->tpu_spans){
    totals.tpu_device_ms+=s.second-s.first;totals.tpu_backend_overlap_ms+=overlap(begin,end,s);
    totals.consume_tpu_ms+=overlap(consume_begin,consume_end,s);
    // Detailed TPU spans on one of every 20 consumed frames; aggregate all.
    if(frame_index%20==0)record("tpu",stamp,s.first,s.second,0,r->clock_uncertainty_ms);
  }
  if(r->cacheable)cache.push_back(r);
}
void TpuLk::flow(const std::vector<cv::Mat>& a,const std::vector<cv::Mat>& b,
    const std::vector<cv::Point2f>& input,std::vector<cv::Point2f>& output,
    std::vector<unsigned char>& status,std::vector<float>& error,cv::Size window,int levels,cv::TermCriteria term,int flags) {
  if(!enabled()) {cv::calcOpticalFlowPyrLK(a,b,input,output,status,error,window,levels,term,flags);return;}
  if(window!=cv::Size(16,15) || levels!=5 || term.maxCount!=30 || std::abs(term.epsilon-.01)>1e-9 || flags!=cv::OPTFLOW_USE_INITIAL_FLOW)
    throw std::runtime_error("TPU LK requires the validated 16x15/5-level/30-iteration configuration");
  if(output.size()!=input.size())output=input;
  status.assign(input.size(),0);error.assign(input.size(),0);
  if(deferred) {
    auto r=prepare(a,b,input,output);r->cacheable=false;execute(r);
    output=r->output;status=r->status;deferred->push_back(r);return;
  }
  std::vector<cv::Point2f> missing,initial;std::vector<size_t> indices;
  std::shared_ptr<void> reusable;
  for(size_t i=0;i<input.size();++i) {
    bool hit=false;
    for(auto &r:cache)if(r->previous[0].data==a[0].data && r->next[0].data==b[0].data) {
      reusable=r->workspace;
      for(size_t k=0;k<r->input.size();++k)if(input[i]==r->input[k] && output[i]==r->initial[k]) {
        output[i]=r->output[k];status[i]=r->status[k];++totals.cache_hits;hit=true;break;
      }
      if(hit)break;
    }
    if(!hit){missing.push_back(input[i]);initial.push_back(output[i]);indices.push_back(i);}
  }
  if(!missing.empty()) {
    // The prefetched request has completed. Its image planes are immutable;
    // newly detected points need fresh control data, not another image copy.
    auto r=prepare(a,b,missing,initial,reusable);execute(r);account(r);
    for(size_t i=0;i<indices.size();++i){output[indices[i]]=r->output[i];status[indices[i]]=r->status[i];}
  }
  cache.erase(std::remove_if(cache.begin(),cache.end(),[&](const std::shared_ptr<Request>& r){return r->previous[0].data==a[0].data && r->next[0].data==b[0].data;}),cache.end());
}
}

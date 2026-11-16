// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine.hpp"
#include "../../../vio-compare/probes/c906-tpu/shared.h"
#include <cvi_sys.h>
#include <cviruntime_context.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#ifdef __aarch64__
#include <arm_neon.h>
#endif
extern "C" int loop_match_make_job(uint8_t*,uint64_t,uint8_t**,uint32_t*);
namespace {
using Clock=std::chrono::steady_clock;
void check(int result,const char* what) { if(result)throw std::runtime_error(std::string(what)+": "+std::to_string(result)); }
double milliseconds(Clock::duration d) {return std::chrono::duration<double,std::milli>(d).count();}
double cpu_ms() {timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
}
struct LoopMatchEngine::Impl {
  std::string backend;
  CVI_U64 physical=0; void* allocation=nullptr;
  CVI_RT_HANDLE runtime=nullptr; CVI_RT_MEM program=nullptr;
  int bridge=-1; bool uncertain=false;
  size_t count=0,queries=0;
  LoopMatchStats stats;
  uint8_t* memory() {return static_cast<uint8_t*>(allocation);}
  tpu_probe_control* control() {return reinterpret_cast<tpu_probe_control*>(memory());}
  loop_match_meta* meta() {return reinterpret_cast<loop_match_meta*>(memory()+LOOP_MATCH_META_OFF);}
  loop_match_result* results() {return reinterpret_cast<loop_match_result*>(memory()+LOOP_MATCH_RESULT_OFF);}
  explicit Impl(const std::string& mode):backend(mode) {
    if(mode!="linux-tpu" && mode!="c906-tpu" && mode!="c906" && mode!="cpu-batch")throw std::runtime_error("Unknown loop match backend: "+mode);
    try {
      if(mode=="cpu-batch")allocation=std::calloc(1,TPU_PROBE_BYTES);
      else check(CVI_SYS_IonAlloc_Cached(&physical,&allocation,"loop_match",TPU_PROBE_BYTES),"loop ION allocation");
      if(!allocation || (mode!="cpu-batch" && (physical<0x80000000u || physical>0x8ff40000u-TPU_PROBE_BYTES)))
        throw std::runtime_error("Loop ION outside supported DDR range");
      std::memset(allocation,0,TPU_PROBE_BYTES);
      control()->magic=TPU_PROBE_MAGIC;control()->version=TPU_PROBE_VERSION;
      control()->operation=LOOP_MATCH_OPERATION;control()->timeout_us=10000;control()->repeats=1;
      meta()->magic=LOOP_MATCH_MAGIC;meta()->version=1;meta()->use_tpu=mode!="c906";
      for(unsigned channel=0;channel<8;++channel)
        for(unsigned i=0;i<256;++i)memory()[LOOP_MATCH_LUT_OFF+channel*256+i]=__builtin_popcount(i);
      uint8_t *commands=nullptr;uint32_t bytes=0;
      if(mode!="cpu-batch")check(loop_match_make_job(memory(),physical,mode=="linux-tpu"?&commands:nullptr,&bytes),"compile Hamming kernel");
      if(mode=="linux-tpu") {
        try {
          check(CVI_RT_Init(&runtime),"TPU runtime init");
          check(CVI_RT_LoadCmdbuf(runtime,commands,bytes,0,0,false,&program),"load Hamming kernel");
        } catch(...) {free(commands);throw;}
        free(commands);
      } else if(mode!="cpu-batch") {
        bridge=open("/dev/c906-tpu-probe",O_RDWR);
        if(bridge<0)throw std::runtime_error("Open C906 bridge: "+std::string(std::strerror(errno)));
      }
      if(mode!="cpu-batch")check(CVI_SYS_IonFlushCache(physical,allocation,TPU_PROBE_BYTES),"initialize loop shared memory");
      std::cerr << "loop_match_backend=" << backend << " batch=" << LOOP_MATCH_BATCH << " shared_bytes=" << TPU_PROBE_BYTES << '\n';
    } catch(...) {cleanup();throw;}
  }
  ~Impl() {cleanup();}
  void cleanup() {
    if(bridge>=0) {close(bridge);bridge=-1;}
    if(program) {CVI_RT_MemFree(runtime,program);program=nullptr;}
    if(runtime) {CVI_RT_DeInit(runtime);runtime=nullptr;}
    // An interrupted mailbox may still own the buffer; retain it until reboot.
    if(allocation && !uncertain) {
      if(backend=="cpu-batch")std::free(allocation);else CVI_SYS_IonFree(physical,allocation);
      allocation=nullptr;
    }
  }
  void flush() {
    if(!count)return;
    meta()->pairs=count;meta()->queries=queries;
    meta()->tpu_calls=meta()->reductions=0;meta()->tpu_ticks=meta()->reduction_ticks=0;
    control()->status=-999;control()->completed=0;
    const auto start=Clock::now();const double cpu=cpu_ms();
    if(backend!="cpu-batch")check(CVI_SYS_IonFlushCache(physical,allocation,LOOP_MATCH_RESULT_OFF+queries*sizeof(loop_match_result)),"flush Hamming inputs");
    if(backend=="cpu-batch") {
      auto* dist=reinterpret_cast<uint16_t*>(memory()+LOOP_MATCH_DIST_OFF);
      for(size_t i=0;i<count;++i) {
        const uint8_t* a=memory()+LOOP_MATCH_A_OFF+i*32;
        const uint8_t* b=memory()+LOOP_MATCH_B_OFF+i*32;
        unsigned d=0;
#ifdef __aarch64__
        d=unsigned(vaddvq_u8(vcntq_u8(veorq_u8(vld1q_u8(a),vld1q_u8(b)))))+
          unsigned(vaddvq_u8(vcntq_u8(veorq_u8(vld1q_u8(a+16),vld1q_u8(b+16)))));
#else
        for(unsigned j=0;j<32;++j)d+=__builtin_popcount(unsigned(a[j]^b[j]));
#endif
        union {uint32_t u;float f;} value;value.f=float(d);dist[i]=value.u>>16;
      }
      check(loop_match_reduce(allocation),"reduce CPU Hamming output");stats.reductions+=count;
    } else if(runtime) {
      const auto device=Clock::now();
      check(CVI_RT_RunCmdbuf(runtime,program,0,0),"run Linux Hamming kernel");
      stats.tpu_ms+=milliseconds(Clock::now()-device);
      check(CVI_SYS_IonInvalidateCache(physical+LOOP_MATCH_DIST_OFF,memory()+LOOP_MATCH_DIST_OFF,LOOP_MATCH_BATCH*2),"invalidate Hamming output");
      check(loop_match_reduce(allocation),"reduce Linux Hamming output");
      ++stats.tpu_calls;stats.reductions+=count;
    } else {
      uint32_t address=physical;
      if(ioctl(bridge,_IOW('T',1,uint32_t),&address)<0) {
        uncertain=true;
        throw std::runtime_error("C906 Hamming mailbox: "+std::string(std::strerror(errno)));
      }
      check(CVI_SYS_IonInvalidateCache(physical,allocation,TPU_PROBE_CONTROL_BYTES),"invalidate C906 control");
      check(CVI_SYS_IonInvalidateCache(physical+LOOP_MATCH_META_OFF,memory()+LOOP_MATCH_META_OFF,64),"invalidate C906 metadata");
      check(CVI_SYS_IonInvalidateCache(physical+LOOP_MATCH_RESULT_OFF,results(),queries*sizeof(loop_match_result)),"invalidate C906 results");
      if(control()->status || control()->completed!=1 || meta()->reductions!=count ||
         meta()->tpu_calls!=(backend=="c906-tpu"?1u:0u))
        throw std::runtime_error("C906 Hamming failed: status="+std::to_string(control()->status)+
          " completed="+std::to_string(control()->completed));
      stats.device_ms+=control()->total_ticks/25000.0;
      stats.tpu_ms+=meta()->tpu_ticks/25000.0;stats.reduction_ms+=meta()->reduction_ticks/25000.0;
      stats.tpu_calls+=meta()->tpu_calls;stats.reductions+=meta()->reductions;
    }
    stats.thread_cpu_ms+=cpu_ms()-cpu;
    stats.submit_ms+=milliseconds(Clock::now()-start);
    ++stats.batches;stats.pairs+=count;count=0;
  }
};
LoopMatchEngine::LoopMatchEngine(const std::string& mode):impl_(new Impl(mode)) {}
LoopMatchEngine::~LoopMatchEngine()=default;
void LoopMatchEngine::begin(size_t queries) {
  if(!queries || queries>LOOP_MATCH_QUERIES || impl_->count)throw std::runtime_error("Invalid Hamming query count");
  impl_->queries=queries;
  for(size_t i=0;i<queries;++i)impl_->results()[i]={257,0xffff};
}
void LoopMatchEngine::append(unsigned query,unsigned candidate,const uint8_t* a,const uint8_t* b) {
  if(query>=impl_->queries || candidate>=LOOP_MATCH_QUERIES || !a || !b)throw std::runtime_error("Invalid Hamming pair");
  const size_t i=impl_->count;
  std::memcpy(impl_->memory()+LOOP_MATCH_A_OFF+i*32,a,32);
  std::memcpy(impl_->memory()+LOOP_MATCH_B_OFF+i*32,b,32);
  reinterpret_cast<loop_match_pair*>(impl_->memory()+LOOP_MATCH_PAIRS_OFF)[i]={uint16_t(query),uint16_t(candidate)};
  if(++impl_->count==LOOP_MATCH_BATCH)impl_->flush();
}
std::vector<loop_match_result> LoopMatchEngine::finish() {
  impl_->flush();return {impl_->results(),impl_->results()+impl_->queries};
}
const LoopMatchStats& LoopMatchEngine::stats() const {return impl_->stats;}

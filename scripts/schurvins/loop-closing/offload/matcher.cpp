// SPDX-License-Identifier: GPL-3.0-or-later
#include "matcher.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <time.h>
namespace {
double thread_ms() {timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;}
}
LoopMatchProvider::LoopMatchProvider(const std::string& backend,bool verify)
  :engine_(backend),backend_(backend),verify_(verify) {}
void LoopMatchProvider::match(const svo::LoopFeatureSets& f,const svo::LoopNodeSets& n,int threshold,Eigen::MatrixXd* out) {
  const auto begin=std::chrono::steady_clock::now();
  const double cpu_begin=thread_ms();
  for(size_t set=0;set<4;++set) {
    if(f[set]->size()!=n[set]->size())throw std::runtime_error("Descriptor/node count mismatch");
    for(const auto& d:*f[set]) if(!d.empty() && (d.type()!=CV_8UC1 || d.total()!=32 || !d.isContinuous()))
      throw std::runtime_error("Offloaded ORB descriptors must be contiguous 32-byte U8");
  }
  const size_t count=f[0]->size()+f[3]->size();
  out->resize(2,count);size_t matches=0;
  if(count && threshold>0) {
    engine_.begin(count);
    using Groups=std::unordered_map<int,std::vector<unsigned>>;
    Groups targets,current;
    for(size_t j=0;j<f[1]->size();++j)if(!f[1]->at(j).empty())targets[n[1]->at(j)].push_back(j);
    for(size_t j=0;j<f[2]->size();++j)if(!f[2]->at(j).empty())current[n[2]->at(j)].push_back(j);
    for(size_t i=0;i<f[0]->size();++i) {
      if(f[0]->at(i).empty())continue;
      auto group=targets.find(n[0]->at(i));if(group==targets.end())continue;
      for(unsigned j:group->second)engine_.append(i,j,f[0]->at(i).ptr<uint8_t>(),f[1]->at(j).ptr<uint8_t>());
    }
    // Original SVO matching is in the reverse direction: past -> current.
    for(size_t i=0;i<f[3]->size();++i) {
      if(f[3]->at(i).empty())continue;
      auto group=current.find(n[3]->at(i));if(group==current.end())continue;
      for(unsigned j:group->second)engine_.append(f[0]->size()+i,j,f[3]->at(i).ptr<uint8_t>(),f[2]->at(j).ptr<uint8_t>());
    }
    const auto best=engine_.finish();
    for(size_t i=0;i<f[0]->size();++i)
      if(best[i].candidate!=0xffff && best[i].distance<threshold)
        out->col(matches++) << i,best[i].candidate;
    for(size_t i=0;i<f[3]->size();++i) {
      const auto& r=best[f[0]->size()+i];
      if(r.candidate!=0xffff && r.distance<threshold)
        out->col(matches++) << r.candidate+f[0]->size(),i+f[1]->size();
    }
  }
  out->conservativeResize(2,matches);
  wall_ms_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  cpu_ms_+=thread_ms()-cpu_begin;
  ++calls_;
  if(verify_) {
    const auto reference_begin=std::chrono::steady_clock::now();
    Eigen::MatrixXd reference;cv::Mat unused1,unused2;
    svo::featureMatchingFastCpu(*f[0],*f[1],*f[2],*f[3],unused1,unused2,*n[0],*n[1],*n[2],*n[3],threshold,&reference);
    reference_ms_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-reference_begin).count();
    if(reference.rows()!=out->rows() || reference.cols()!=out->cols() ||
       (reference.size() && (reference-*out).cwiseAbs().maxCoeff()!=0))
      throw std::runtime_error("Offloaded matches differ from upstream CPU reference");
    ++verified_;
  }
}
void LoopMatchProvider::report() const {
  const auto& s=engine_.stats();
  std::cout << "loop_offload: backend=" << backend_ << " calls=" << calls_ << " verified_calls=" << verified_
    << " pairs=" << s.pairs << " batches=" << s.batches << " tpu_calls=" << s.tpu_calls
    << " reductions=" << s.reductions << " matching_wall_ms=" << wall_ms_ << " matching_thread_cpu_ms=" << cpu_ms_
    << " reference_wall_ms=" << reference_ms_ << " submit_ms=" << s.submit_ms
    << " device_ms=" << s.device_ms << " tpu_ms=" << s.tpu_ms << " reduction_ms=" << s.reduction_ms
    << " submit_thread_cpu_ms=" << s.thread_cpu_ms << '\n';
}

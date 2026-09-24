// SPDX-License-Identifier: GPL-3.0-or-later
#include "imu_prediction.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
Eigen::Vector3d vec(const std::array<double,3>& a) { return {a[0],a[1],a[2]}; }
ImuSample interpolate(const ImuSample& a, const ImuSample& b, uint64_t t) {
  ImuSample s{}; s.stamp_ns=t;
  const double u=double(t-a.stamp_ns)/double(b.stamp_ns-a.stamp_ns);
  for(int i=0;i<3;++i) {
    s.gyro[i]=a.gyro[i]+u*(b.gyro[i]-a.gyro[i]);
    s.accel[i]=a.accel[i]+u*(b.accel[i]-a.accel[i]);
  }
  return s;
}
void step(PredictedState& s, const ImuSample& a, const ImuSample& b) {
  const double dt=(b.stamp_ns-a.stamp_ns)*1e-9;
  const Eigen::Vector3d angle=((vec(a.gyro)+vec(b.gyro))*0.5-s.bg)*dt;
  Eigen::Quaterniond dq=Eigen::Quaterniond::Identity();
  const double theta=angle.norm();
  if(theta>1e-12) dq=Eigen::Quaterniond(Eigen::AngleAxisd(theta,angle/theta));
  else { dq.coeffs().head<3>()=angle*0.5; dq.normalize(); }
  const Eigen::Quaterniond next=(s.q*dq).normalized();
  const Eigen::Vector3d acc=0.5*(s.q*(vec(a.accel)-s.ba)+next*(vec(b.accel)-s.ba))+s.gravity;
  s.p+=s.v*dt+0.5*acc*dt*dt; s.v+=acc*dt; s.q=next; s.stamp_ns=b.stamp_ns;
}
}
struct ImuPrediction::Impl {
  struct Event { int type=0; ImuSample sample{}; PredictedState state; };
  Sink sink;
  const bool threaded;
  const uint64_t max_age;
  mutable std::mutex mutex;
  std::condition_variable wake;
  std::deque<Event> events;
  std::thread worker;
  bool stopped=false;
  std::string error;
  std::deque<ImuSample> history;
  PredictedState state;
  ImuSample previous{};
  uint64_t anchor=0,last_input=0,last_output=0,last_correction=0;
  bool valid=false,announced=false;
  Impl(Sink callback,bool async,uint64_t age):sink(std::move(callback)),threaded(async),max_age(age) {
    if(threaded) worker=std::thread([this]{run();});
  }
  void failTracking(uint64_t t) {
    valid=false;
    if(announced) { PredictedState invalid=state; invalid.stamp_ns=std::max(t,last_output); sink(invalid,false); announced=false; }
  }
  void publish() {
    if(state.stamp_ns-anchor>max_age) { failTracking(state.stamp_ns); return; }
    if(state.stamp_ns<=last_output) return; // corrections never resend an old timestamp
    sink(state,true); last_output=state.stamp_ns; announced=true;
  }
  void process(const Event& e) {
    if(e.type==2) { failTracking(last_input); return; }
    if(e.type==0) {
      const auto& s=e.sample;
      if(!s.stamp_ns || (last_input && s.stamp_ns<=last_input)) throw std::runtime_error("Prediction IMU timestamps not increasing");
      if(!vec(s.accel).allFinite() || !vec(s.gyro).allFinite()) throw std::runtime_error("Prediction IMU non-finite");
      if(last_input && s.stamp_ns-last_input>=15000000) { failTracking(s.stamp_ns); history.clear(); }
      last_input=s.stamp_ns; history.push_back(s);
      while(history.size()>2000) history.pop_front();
      if(valid) { step(state,previous,s); previous=s; publish(); }
      return;
    }
    const auto& c=e.state;
    if(c.stamp_ns<=last_correction) return;
    if(!c.p.allFinite() || !c.v.allFinite() || !c.ba.allFinite() || !c.bg.allFinite() ||
       !c.gravity.allFinite() || !c.q.coeffs().allFinite() || c.q.norm()<0.5)
      throw std::runtime_error("Invalid visual correction state");
    last_correction=c.stamp_ns;
    // Need an IMU sample on each side, including an exact-time left endpoint.
    if(history.empty() || history.front().stamp_ns>c.stamp_ns || history.back().stamp_ns<c.stamp_ns) {
      failTracking(last_input); return;
    }
    auto after=std::upper_bound(history.begin(),history.end(),c.stamp_ns,
      [](uint64_t t,const ImuSample& s){return t<s.stamp_ns;});
    const auto before=std::prev(after);
    state=c; state.q.normalize(); anchor=c.stamp_ns;
    previous=before->stamp_ns==c.stamp_ns ? *before : interpolate(*before,*after,c.stamp_ns);
    valid=true;
    for(auto it=after;it!=history.end();++it) { step(state,previous,*it); previous=*it; }
    while(history.size()>1 && history[1].stamp_ns<=anchor) history.pop_front();
    publish();
  }
  void enqueue(Event e) {
    if(!threaded) { process(e); return; }
    std::lock_guard<std::mutex> lock(mutex);
    if(!error.empty()) throw std::runtime_error(error);
    if(stopped) throw std::runtime_error("Prediction already stopped");
    if(events.size()>=2048) throw std::runtime_error("Prediction worker queue full");
    events.push_back(std::move(e)); wake.notify_one();
  }
  void run() noexcept {
    try {
      for(;;) {
        Event e;
        { std::unique_lock<std::mutex> lock(mutex); wake.wait(lock,[this]{return stopped||!events.empty();});
          if(events.empty()) break;
          e=events.front(); events.pop_front(); }
        process(e);
      }
    } catch(const std::exception& ex) { std::lock_guard<std::mutex> lock(mutex); error=ex.what(); stopped=true; }
  }
  void stop() { {std::lock_guard<std::mutex> lock(mutex);stopped=true;} wake.notify_all(); if(worker.joinable())worker.join(); }
};
ImuPrediction::ImuPrediction(Sink sink,bool threaded,uint64_t age):impl_(new Impl(std::move(sink),threaded,age)) {}
ImuPrediction::~ImuPrediction(){impl_->stop();}
void ImuPrediction::sample(const ImuSample& s){Impl::Event e;e.sample=s;impl_->enqueue(e);}
void ImuPrediction::correct(const PredictedState& s){Impl::Event e;e.type=1;e.state=s;impl_->enqueue(e);}
void ImuPrediction::invalidate(){Impl::Event e;e.type=2;impl_->enqueue(e);}
void ImuPrediction::check()const{std::lock_guard<std::mutex> lock(impl_->mutex);if(!impl_->error.empty())throw std::runtime_error(impl_->error);}
void ImuPrediction::finish(){impl_->stop();check();}

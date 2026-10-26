// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef BENCH_NATIVE
#include "native_bridge.hpp"
using Bridge=NativeBridge;
#else
#include "../micro_ros_bridge.hpp"
using Bridge=MicroRosBridge;
#endif
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
namespace {
double cpu() {timespec t{};if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t)) throw std::runtime_error("cpu clock");return t.tv_sec+t.tv_nsec*1e-9;}
void status() {
 std::ifstream file("/proc/self/status");std::string line;
 while(std::getline(file,line)) if(line.find("VmRSS:")==0||line.find("VmSize:")==0||line.find("Threads:")==0) std::cerr<<line<<'\n';
}
}
int main(int argc,char** argv) {
 try {
  if(argc!=3) throw std::runtime_error("Usage: benchmark HZ SECONDS");
  const unsigned hz=std::stoul(argv[1]),seconds=std::stoul(argv[2]);
  if((hz!=20&&hz!=200)||seconds<5||seconds>120) throw std::runtime_error("invalid rate/duration");
  const double startup_cpu=cpu();auto startup=std::chrono::steady_clock::now();Bridge bridge;
  const double p[3]={1.25,-2.5,3.75},q[4]={0,0,0,1};
  const auto period=std::chrono::nanoseconds(1000000000ULL/hz);
  const unsigned warmup=hz*5;
  auto next=std::chrono::steady_clock::now();
  auto submit=[&](unsigned i) {bridge.submit(1700000000000000000ULL+uint64_t(i)*(1000000000ULL/hz),true,p,q);next+=period;std::this_thread::sleep_until(next);};
  for(unsigned i=0;i<warmup;++i) submit(i);
  const double warm_cpu=cpu()-startup_cpu;
  const double warm_wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-startup).count();
  std::cerr<<"MEASURE_START pid="<<getpid()<<'\n';status();
  const double begin_cpu=cpu();const auto begin=std::chrono::steady_clock::now();
  for(unsigned i=warmup;i<warmup+hz*seconds;++i) submit(i);
  const double measured_cpu=cpu()-begin_cpu;
  const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
  std::cerr<<"MEASURE_END\n";status();rusage usage{};getrusage(RUSAGE_SELF,&usage);
  std::cout<<std::setprecision(10)<<"{\"hz\":"<<hz<<",\"seconds\":"<<seconds<<",\"samples\":"<<hz*seconds
    <<",\"wall_s\":"<<wall<<",\"cpu_s\":"<<measured_cpu<<",\"cpu_percent\":"<<100*measured_cpu/wall
    <<",\"warmup_cpu_s\":"<<warm_cpu<<",\"warmup_wall_s\":"<<warm_wall<<",\"maxrss_kib\":"<<usage.ru_maxrss<<",\"enabled\":"<<(bridge.enabled()?"true":"false")<<"}\n"<<std::flush;
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#include "live_imu.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <time.h>
static volatile sig_atomic_t interrupted;
static void stop(int) { interrupted=1; }
static uint64_t ns() { timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec; }
int main(int argc,char**argv) {
 try {
  if (argc<2 || argc>3) throw std::runtime_error("Usage: schurvins_imu_test DEVICE [SECONDS=10]");
  size_t parsed=0;
  int seconds=argc==3 ? std::stoi(argv[2],&parsed) : 10;
  if (argc==3 && argv[2][parsed]) throw std::runtime_error("Invalid seconds");
  if (seconds<1 || seconds>3600) throw std::runtime_error("Seconds must be 1..3600");
  signal(SIGINT,stop); signal(SIGTERM,stop);
  LiveImu imu(argv[1]); uint64_t end=ns()+uint64_t(seconds)*1000000000ULL;
  uint64_t first=0,last=0,count=0; double norm_sum=0,gyro_sum[3]{},acc_sum[3]{};
  while (!interrupted && ns()<end) {
   for (const auto& sample:imu.through(ns(),interrupted)) {
    if (!first) first=sample.stamp_ns;
    if (last && sample.stamp_ns<=last) throw std::runtime_error("Non-monotonic IMU");
    last=sample.stamp_ns; ++count; double norm=0;
    for(int i=0;i<3;++i) { acc_sum[i]+=sample.accel[i]; gyro_sum[i]+=sample.gyro[i]; norm+=sample.accel[i]*sample.accel[i]; }
    norm_sum+=std::sqrt(norm);
   }
  }
  imu.stop(); imu.check(); imu.report();
  if (count<2) throw std::runtime_error("No IMU data");
  double rate=(count-1)*1e9/(last-first);
  std::cout<<"test_rate_hz="<<rate<<" accel_norm_m_s2="<<norm_sum/count
   <<" gyro_rad_s="<<gyro_sum[0]/count<<","<<gyro_sum[1]/count<<","<<gyro_sum[2]/count
   <<" accel_m_s2="<<acc_sum[0]/count<<","<<acc_sum[1]/count<<","<<acc_sum[2]/count<<'\n';
  if(interrupted || rate<190 || rate>210 || norm_sum/count<4.9 || norm_sum/count>14.8)
   throw std::runtime_error("IMU rate/static gravity check failed (keep board stationary)");
  std::cout<<"SC7U22 200HZ PASS (SI units, increasing host timestamps; not calibrated accuracy)\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<'\n';return 1;}
}

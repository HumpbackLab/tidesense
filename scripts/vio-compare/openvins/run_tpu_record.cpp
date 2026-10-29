// Fixed-input replay for optional C906/TPU LK; estimator backend remains FP64.
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <type_traits>
#include <vector>
#include <time.h>
#include <unistd.h>
#include <opencv2/imgcodecs.hpp>
#include "core/VioManager.h"
#include "core/VioManagerOptions.h"
#include "state/State.h"
#include "state/StateHelper.h"
#include "utils/opencv_yaml_parse.h"
#include "utils/print.h"
#ifdef OPENVINS_C906_TPU_LK
#include "tpu_lk.hpp"
#ifdef OPENVINS_C906_FRONTEND
#include "front_tpu.hpp"
#endif
#endif
static_assert(std::is_same<decltype(ov_msckf::StateHelper::get_full_covariance(
    std::declval<std::shared_ptr<ov_msckf::State>>())),Eigen::MatrixXd>::value,"FP64 backend required");
static std::vector<std::string> fields(const std::string &s) {
  std::stringstream stream(s);std::string f;std::vector<std::string> out;
  while(std::getline(stream,f,','))out.push_back(f);return out;
}
static double cpu_time_ms(clockid_t id) {
  timespec t;clock_gettime(id,&t);return t.tv_sec*1000.0+t.tv_nsec*1e-6;
}
struct SystemCpu {
  uint64_t total=0,idle=0,iowait=0;
  static SystemCpu read() {
    std::ifstream f("/proc/stat");std::string line,label;std::getline(f,line);std::istringstream in(line);in>>label;
    SystemCpu out;for(int i=0;i<8;++i){uint64_t x=0;in>>x;out.total+=x;if(i==3)out.idle=x;if(i==4)out.iowait=x;}return out;
  }
};
int main(int argc,char **argv) {
  if(argc<5 || argc>6){std::cerr<<"usage: run_tpu_record config.yaml record_dir poses.csv frame_limit [right_rotation=180]\n";return 2;}
  try {
    std::string root=argv[2];int limit=std::stoi(argv[4]),rotation=argc==6?std::stoi(argv[5]):180;
    if(rotation!=0 && rotation!=180)throw std::runtime_error("right rotation must be 0 or 180");
    auto parser=std::make_shared<ov_core::YamlParser>(argv[1]);ov_core::Printer::setPrintLevel("WARNING");
    ov_msckf::VioManagerOptions options;options.print_and_load(parser);
    options.num_opencv_threads=1;options.use_multi_threading_pubs=false;options.use_multi_threading_subs=false;
    if(!parser->successful())throw std::runtime_error("config parse failed");
    ov_msckf::VioManager system(options);
    std::ifstream imu_file(root+"/imu.csv"),frame_file(root+"/stereo.csv");
    std::vector<ov_core::ImuData> imu;std::vector<std::vector<std::string>> rows;std::string line;
    while(std::getline(imu_file,line)) {
      if(line.empty() || line[0]=='#')continue;auto f=fields(line);if(f.size()<7)throw std::runtime_error("bad IMU CSV");
      ov_core::ImuData m;m.timestamp=std::stod(f[0])*1e-9;
      m.wm<<std::stod(f[1]),std::stod(f[2]),std::stod(f[3]);m.am<<std::stod(f[4]),std::stod(f[5]),std::stod(f[6]);imu.push_back(m);
    }
    while(std::getline(frame_file,line))if(!line.empty() && line[0]!='#') {
      auto f=fields(line);if(f.size()<3)throw std::runtime_error("bad stereo CSV");rows.push_back(f);
    }
    if(imu.empty() || rows.empty())throw std::runtime_error("empty dataset");
    if(limit>0 && rows.size()>size_t(limit))rows.resize(limit);
    auto disk_load=[&](size_t i) {
      ov_core::CameraData m;m.timestamp=std::stoull(rows[i][0])*1e-9;m.sensor_ids={0,1};
      for(int c=1;c<=2;++c){cv::Mat im=cv::imread(root+"/"+rows[i][c],cv::IMREAD_GRAYSCALE);if(im.empty())throw std::runtime_error("image read failed");m.images.push_back(im);}
      if(rotation)cv::flip(m.images[1],m.images[1],-1);
      m.masks={cv::Mat::zeros(m.images[0].size(),CV_8UC1),cv::Mat::zeros(m.images[1].size(),CV_8UC1)};return m;
    };
    // Cache only a bounded window: enough to expose accelerator waits without
    // synchronous SD image reads, while retaining the exact estimator prefix.
    size_t preload_first=0,preload_count=0;
    if(const char *v=std::getenv("OPENVINS_PRELOAD_FIRST"))preload_first=std::stoull(v);
    if(const char *v=std::getenv("OPENVINS_PRELOAD_COUNT"))preload_count=std::stoull(v);
    if(preload_count>100 || preload_first>rows.size())throw std::runtime_error("Preload range exceeds 100 pairs or dataset");
    preload_count=std::min(preload_count,rows.size()-preload_first);
    std::vector<ov_core::CameraData> preloaded;
    for(size_t i=0;i<preload_count;++i)preloaded.push_back(disk_load(preload_first+i));
    auto load=[&](size_t i){return i>=preload_first && i<preload_first+preload_count?
        preloaded[i-preload_first]:disk_load(i);};
    if(preload_count)std::cerr<<"PRELOAD first="<<preload_first<<" count="<<preload_count<<" pairs before timing\n";
    std::ofstream poses(argv[3]);if(!poses)throw std::runtime_error("pose file open failed");
    poses<<"timestamp_ns,initialized,px,py,pz,qx,qy,qz,qw,compute_ms,compute_main_cpu_ms,compute_process_cpu_ms,tpu_hits,tpu_points,tpu_prepare_ms,tpu_remote_ms,tpu_wait_cpu_ms,tpu_overlap_ms";
#ifdef OPENVINS_C906_TPU_LK
    poses<<ov_core::TpuLk::diagnostic_header();
#ifdef OPENVINS_C906_FRONTEND
    poses<<ov_core::TpuFront::diagnostic_header();
#endif
#endif
    poses<<'\n'<<std::setprecision(15);
    auto image=load(0);size_t cursor=0;unsigned initialized=0;double total=0;auto begin=std::chrono::steady_clock::now();
    const double process_begin=cpu_time_ms(CLOCK_PROCESS_CPUTIME_ID);const auto system_begin=SystemCpu::read();
    for(size_t i=0;i<rows.size();++i) {
      while(cursor<imu.size() && imu[cursor].timestamp<=image.timestamp+.006)system.feed_measurement_imu(imu[cursor++]);
      ov_core::CameraData next;bool loaded=false;
#ifdef OPENVINS_C906_TPU_LK
      if(ov_core::TpuLk::pipeline() && i+1<rows.size()){next=load(i+1);loaded=true;system.set_next_camera_for_lk(next);}
      auto old=ov_core::TpuLk::stats();
#ifdef OPENVINS_C906_FRONTEND
      auto front_before=ov_core::TpuFront::stats();
#endif
#endif
      auto t=std::chrono::steady_clock::now();
      const double main_cpu=cpu_time_ms(CLOCK_THREAD_CPUTIME_ID),process_cpu=cpu_time_ms(CLOCK_PROCESS_CPUTIME_ID);
      system.feed_measurement_camera(image);
      double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();total+=ms;
      bool ready=system.initialized();initialized+=ready;poses<<rows[i][0]<<','<<ready;
      if(ready) {
        auto state=system.get_state();auto p=state->_imu->pos();auto q=state->_imu->quat();
        if(!p.allFinite() || !q.allFinite())throw std::runtime_error("non-finite pose");
        poses<<','<<p[0]<<','<<p[1]<<','<<p[2]<<','<<q[0]<<','<<q[1]<<','<<q[2]<<','<<q[3];
      } else poses<<",,,,,,,";
      poses<<','<<ms<<','<<cpu_time_ms(CLOCK_THREAD_CPUTIME_ID)-main_cpu<<','<<cpu_time_ms(CLOCK_PROCESS_CPUTIME_ID)-process_cpu;
#ifdef OPENVINS_C906_TPU_LK
      auto now=ov_core::TpuLk::stats();
      poses<<','<<now.cache_hits-old.cache_hits<<','<<now.points-old.points<<','<<now.prepare_ms-old.prepare_ms
           <<','<<now.remote_ms-old.remote_ms<<','<<now.wait_cpu_ms-old.wait_cpu_ms<<','<<now.overlap_ms-old.overlap_ms
           <<ov_core::TpuLk::diagnostic_delta(old);
#ifdef OPENVINS_C906_FRONTEND
      poses<<ov_core::TpuFront::diagnostic_delta(front_before);
#endif
#else
      poses<<",0,0,0,0,0,0";
#endif
      poses<<'\n';if(!poses)throw std::runtime_error("pose write failed");
      if((i+1)%100==0)std::cerr<<"frames="<<i+1<<" initialized="<<initialized<<" avg_ms="<<total/(i+1)<<'\n';
      if(i+1<rows.size())image=loaded?next:load(i+1);
    }
    rusage u{};getrusage(RUSAGE_SELF,&u);
    const auto system_end=SystemCpu::read();const double hz=sysconf(_SC_CLK_TCK);
    std::cerr<<"CPU process_s="<<(cpu_time_ms(CLOCK_PROCESS_CPUTIME_ID)-process_begin)/1000
             <<" system_total_s="<<(system_end.total-system_begin.total)/hz
             <<" system_idle_s="<<(system_end.idle-system_begin.idle)/hz
             <<" system_iowait_s="<<(system_end.iowait-system_begin.iowait)/hz<<'\n';
    std::cerr<<"DONE frames="<<rows.size()<<" initialized="<<initialized<<" avg_ms="<<total/rows.size()
             <<" wall_s="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<" peak_rss_kb="<<u.ru_maxrss<<'\n';
    return 0;
  }catch(const std::exception &e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}

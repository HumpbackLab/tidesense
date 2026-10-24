// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline.hpp"
#include "combined_ros.hpp"
#include "live_camera.hpp"
#include "live_imu.hpp"
#include "imu_intrinsics.hpp"
#include <svo/schur_vins.h>
#include <svo/frame_handler_stereo.h>
#include <svo/imu_handler.h>
#include <svo/initialization.h>
#include <svo/reprojector.h>
#include <svo/stereo_triangulation.h>
#include <svo/direct/depth_filter.h>
#include <svo/direct/feature_detection.h>
#include <svo/tracker/feature_tracker.h>
#include <svo/common/frame.h>
#include <glog/logging.h>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <time.h>
#include <sys/resource.h>
namespace {
volatile sig_atomic_t stopped=0;
void stop(int) { stopped=1; }
uint64_t now(clockid_t clock=CLOCK_MONOTONIC) {
  timespec t{};if(clock_gettime(clock,&t)) throw std::runtime_error("clock_gettime");
  return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
uint64_t integer(const std::string& s) {
  if(s.empty() || s.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("Expected unsigned integer");
  return std::stoull(s);
}
struct DepthResult { SadResult sad;cv::Mat reference,depth;PipelineTiming timing;uint64_t end=0; };
// Exactly one outstanding immutable capture. There is no second camera consumer
// and no lookup of a 'latest' depth/pose when joining results.
class DepthWorker {
  struct Job { std::array<cv::Mat,2> filtered;PipelineTiming timing;std::promise<DepthResult> promise; };
  DepthPipeline pipeline_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::shared_ptr<Job> next_;
  bool stop_=false;
  std::thread thread_;
 public:
  explicit DepthWorker(const std::string& calibration):pipeline_(calibration),thread_([this]{
    setpriority(PRIO_PROCESS,0,-3); // Short TPU replay calls should not wait behind a full VIO slice.
    for(;;) {
      std::shared_ptr<Job> job;
      { std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this]{return stop_ || bool(next_);});
        if(!next_) return;job=std::move(next_); }
      try { DepthResult result;result.timing=job->timing;result.reference=job->filtered[0];
        result.sad=pipeline_.match(job->filtered,result.timing);
        result.end=now();job->promise.set_value(std::move(result)); }
      catch(...) { job->promise.set_exception(std::current_exception()); }
    }
  }) {}
  ~DepthWorker() { { std::lock_guard<std::mutex> lock(mutex_);stop_=true; }wake_.notify_one();thread_.join(); }
  const DepthIntrinsics& intrinsics() const { return pipeline_.intrinsics(); }
  void finish(DepthResult& result) { result.depth=pipeline_.finish(result.sad,result.reference,result.timing);result.end=now(); }
  std::future<DepthResult> submit(const LivePair& pair) {
    auto job=std::make_shared<Job>();job->filtered=pipeline_.prepare(pair.left,pair.right,job->timing);
    auto future=job->promise.get_future();
    { std::lock_guard<std::mutex> lock(mutex_);if(next_) throw std::runtime_error("Depth queue overflow");next_=job; }
    wake_.notify_one();return future;
  }
};
}
int main(int argc,char** argv) try {
  const auto bin=executableDirectory();
  std::string calibration=bin+"/../calibration.yaml",ini=bin+"/../sensor_cfg.ini";
  std::string helper=bin+"/schurvins_camera",device="auto",profile_path;
  unsigned features=60,seeds=90,keyframes=3,window=3,detector_min=1,fast_threshold=15;
  uint64_t limit=0;bool no_ros=false;
  if(const char* s=std::getenv("TIDESENSE_VIO_DEPTH_PROFILE")) profile_path=s;
  for(int i=1;i<argc;++i) {
    std::string option=argv[i];
    if(option=="--help") {
      std::cout << "tidesense_vio_depth [--calibration FILE] [--sensor-ini FILE] [--imu auto|/dev/iio:deviceN]\n"
        "  [--camera-helper FILE] [--frames N] [--profile CSV] [--no-ros]\n"
        "  [--detector-min-level 0|1] [--fast-threshold 5..50]\n"
        "  [--features 30..120] [--seeds FEATURES..240] [--keyframes 2..5] [--window 2..4]\n"
        "Shared FSIN pair: SchurVINS 640x400 on A53, TPU area resize 320x200 + D32 SAD.\n"
        "micro-ROS: SCHUR_MICRO_ROS_AGENT=HOST_IP, optional SCHUR_MICRO_ROS_PORT/DOMAIN.\n"
        "Topics: /tidesense/pose (T_W_B), /tidesense/depth/image_raw (16UC1 mm), depth/camera_info.\n"
        "Same acquisition stamp; pose only during tracking. --no-ros is for profiling.\n";return 0;
    }
    if(option=="--no-ros") { no_ros=true;continue; }
    if(++i==argc) throw std::runtime_error("Missing option value");
    std::string value=argv[i];
    if(option=="--calibration") calibration=value;
    else if(option=="--sensor-ini") ini=value;
    else if(option=="--camera-helper") helper=value;
    else if(option=="--imu") device=value;
    else if(option=="--frames") limit=integer(value);
    else if(option=="--detector-min-level") detector_min=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--fast-threshold") fast_threshold=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--features") features=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--seeds") seeds=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--keyframes") keyframes=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--window") window=unsigned(std::min<uint64_t>(integer(value),UINT32_MAX));
    else if(option=="--profile") profile_path=value;
    else throw std::runtime_error("Unknown option: "+option);
  }
  if(detector_min>1 || fast_threshold<5 || fast_threshold>50 || features<30 || features>120 || seeds<features || seeds>240 || keyframes<2 || keyframes>5 || window<2 || window>4)
    throw std::runtime_error("Invalid VIO budget: features 30..120, seeds features..240, keyframes 2..5, window 2..4");
  google::InitGoogleLogging(argv[0]);FLAGS_logtostderr=true;
  cv::setNumThreads(1);cv::setUseOptimized(true);
  auto yaml=YAML::LoadFile(calibration);
  if((yaml["test_only"] && yaml["test_only"].as<bool>()) ||
      (yaml["label"] && yaml["label"].as<std::string>().find("SYNTHETIC")!=std::string::npos))
    throw std::runtime_error("Use real stereo/IMU calibration");
  auto intrinsics=ImuIntrinsics::parse(yaml["imu_params"]);
  if(!yaml["imu_params"]["input_domain"] || yaml["imu_params"]["input_domain"].as<std::string>()!="raw_si" || intrinsics.input_is_corrected)
    throw std::runtime_error("Live IMU requires uncorrected raw_si input");
  auto cams=svo::CameraBundle::loadFromYaml(calibration);
  if(!cams || cams->numCameras()!=2) throw std::runtime_error("Two cameras required");
  for(int i=0;i<2;++i) if(cams->getCamera(i).imageWidth()!=640 || cams->getCamera(i).imageHeight()!=400)
    throw std::runtime_error("VIO requires 640x400 calibration");
  auto imu_cal=svo::ImuHandler::loadCalibrationFromFile(calibration);
  if(imu_cal.imu_rate!=200 || imu_cal.delay_imu_cam!=0) throw std::runtime_error("Require 200Hz IMU and pre-aligned camera timestamps");
  auto imu=std::make_shared<svo::ImuHandler>(imu_cal,svo::ImuHandler::loadInitializationFromFile(calibration),svo::IMUHandlerOptions());
  int64_t camera_offset=yaml["live_camera_time_offset_ns"]?yaml["live_camera_time_offset_ns"].as<int64_t>():0;
  if(camera_offset < -1000000000LL || camera_offset > 1000000000LL) throw std::runtime_error("Camera offset exceeds one second");
  svo::BaseOptions base;base.use_imu=true;base.max_n_kfs=keyframes;base.window_size_=window;base.quality_min_fts=20;
  base.img_align_max_level=3;base.img_align_min_level=1;base.trace_statistics=false;
  svo::DetectorOptions detector;detector.cell_size=25;detector.detector_type=svo::DetectorType::kFast;detector.threshold_primary=fast_threshold;detector.min_level=detector_min;
  svo::ReprojectorOptions reproj;reproj.cell_size=25;reproj.max_n_features_per_frame=features;
  svo::DepthFilterOptions depth;depth.use_threaded_depthfilter=false;depth.max_n_seeds_per_frame=seeds;
  svo::InitializationOptions init;init.init_type=svo::InitializerType::kStereo;init.init_min_features=20;
  svo::FrameHandlerStereo vio(base,depth,detector,init,svo::StereoTriangulationOptions(),reproj,svo::FeatureTrackerOptions(),cams);
  vio.setImuHandler(imu);vio.start();
  DepthWorker worker(calibration);
  std::unique_ptr<CombinedRos> ros;
  if(!no_ros) ros.reset(new CombinedRos(worker.intrinsics()));
  std::ofstream profile;
  if(!profile_path.empty()) {
    profile.exceptions(std::ios::failbit|std::ios::badbit);profile.open(profile_path);
    profile << "frame,sequence,left_ns,right_ns,stamp_ns,ros_ns,begin_ns,vio_end_ns,depth_end_ns,end_ns,vio_cpu_ms,depth_cpu_ms,downsample_ms,rectify_ms,prefilter_ms,match_ms,filter_ms,tracking,imu_ready,tracking_stage,tracking_quality,valid_depth,ros_queued,px,py,pz,qx,qy,qz,qw\n";
  }
  struct sigaction sa{};sa.sa_handler=stop;sigaction(SIGINT,&sa,nullptr);sigaction(SIGTERM,&sa,nullptr);
  int64_t ros_offset=int64_t(now(CLOCK_REALTIME))-int64_t(now());
  LiveCamera camera(helper,ini);
  { LivePair warmup;if(!camera.next(warmup,stopped)) return 0; }
  LiveImu live_imu(device);
  uint64_t ready=now()+100000000ULL,last_imu=0,last_image=0;
  uint64_t frames=0,tracked=0,fused=0,queued=0,first=0,last=0,previous_sequence=0,skipped=0,overruns=0;
  double total_wall=0;
  std::cerr << "vio-depth: one capture, A53 SchurVINS 640x400 + TPU area 320x200 + quality SAD D32; nominal 20Hz\n";
  std::cerr << "VIO budget: detector_min=" << detector_min << " FAST=" << fast_threshold << " features=" << features
    << " seeds=" << seeds << " keyframes=" << keyframes << " window=" << window << '\n';
  while(!stopped && (!limit || frames<limit)) {
    LivePair pair;if(!camera.next(pair,stopped)) break;
    auto& h=pair.header;
    if(!(h.flags & CAMERA_FSIN)) throw std::runtime_error("Both cameras require FSIN hardware synchronization");
    if((h.left_ns>h.right_ns?h.left_ns-h.right_ns:h.right_ns-h.left_ns)>10000000ULL)
      throw std::runtime_error("Stereo VI skew exceeds 10ms");
    if(h.left_ns>uint64_t(INT64_MAX)-1000000000ULL || (camera_offset<0 && h.left_ns<=uint64_t(-camera_offset)))
      throw std::runtime_error("Camera timestamp overflow");
    uint64_t t=uint64_t(int64_t(h.left_ns)+camera_offset);
    if(t<ready) continue;
    if(t<=last_image || (frames && h.sequence<=previous_sequence)) throw std::runtime_error("Non-increasing capture identity");
    if(frames) skipped+=h.sequence-previous_sequence-1;
    previous_sequence=h.sequence;last_image=t;
    if(!first) first=t;last=t;
    auto samples=live_imu.through(t,stopped);if(stopped) break;
    for(auto sample:samples) {
      if(sample.stamp_ns<=last_imu || (!last_imu && sample.stamp_ns>=t)) throw std::runtime_error("Invalid IMU interpolation coverage");
      last_imu=sample.stamp_ns;sample=intrinsics.apply(sample);
      imu->addImuMeasurement(svo::ImuMeasurement(last_imu*1e-9,
        Eigen::Vector3d(sample.gyro[0],sample.gyro[1],sample.gyro[2]),
        Eigen::Vector3d(sample.accel[0],sample.accel[1],sample.accel[2])));
    }
    if(last_imu<=t) throw std::runtime_error("IMU does not extend past the image");
    uint64_t begin=now();
    auto future=worker.submit(pair);uint64_t cpu_begin=now(CLOCK_THREAD_CPUTIME_ID);
    svo::Quaternion rotation;bool attitude=true;
    if(!vio.hasStarted()) {
      attitude=imu->getInitialAttitude(t*1e-9,rotation);
      if(attitude) vio.setRotationPrior(rotation);
    } else if(vio.getLastFrames() && imu->getRelativeRotationPrior(vio.getLastFrames()->getMinTimestampSeconds(),t*1e-9,false,rotation))
      vio.setRotationIncrementPrior(rotation);
    if(attitude) vio.addImageBundle({pair.left,pair.right},t);
    uint64_t vio_end=now();double vio_cpu=(now(CLOCK_THREAD_CPUTIME_ID)-cpu_begin)*1e-6;
    VisualPose pose;auto f=vio.getLastFrames();bool imu_ready=f && f->imu_ready_;
    if(attitude && f && imu_ready && vio.stage()==svo::Stage::kTracking &&
        vio.trackingQuality()!=svo::TrackingQuality::kInsufficient && uint64_t(f->getMinTimestampNanoseconds())==t) {
      auto T=f->get_T_W_B();auto p=T.getPosition();auto q=T.getEigenQuaternion();
      if(!p.allFinite() || !q.coeffs().allFinite()) throw std::runtime_error("Nonfinite VIO pose");
      pose.valid=true;for(int i=0;i<3;++i) pose.p[i]=p[i];for(int i=0;i<4;++i) pose.q[i]=q.coeffs()[i];++tracked;
    }
    fused+=imu_ready;
    auto result=future.get();worker.finish(result); // Joins precisely this immutable capture with this VIO update.
    if(int64_t(t)+ros_offset<0) throw std::runtime_error("Invalid ROS timestamp");
    uint64_t ros_stamp=uint64_t(int64_t(t)+ros_offset);
    bool accepted=ros && ros->submit(ros_stamp,result.depth,pose);queued+=accepted;
    uint64_t end=now();double wall=(end-begin)*1e-6;total_wall+=wall;overruns+=wall>50;
    if(profile.is_open()) {
      auto& d=result.timing;
      profile << frames << ',' << h.sequence << ',' << h.left_ns << ',' << h.right_ns << ',' << t << ',' << ros_stamp
        << ',' << begin << ',' << vio_end << ',' << result.end << ',' << end << std::setprecision(10)
        << ',' << vio_cpu << ',' << d.cpu << ',' << d.downsample << ',' << d.rectify << ',' << d.prefilter << ',' << d.match << ',' << d.filter
        << ',' << pose.valid << ',' << imu_ready << ',' << svo::kStageName.at(vio.stage())
        << ',' << svo::kTrackingQualityName.at(vio.trackingQuality()) << ',' << cv::countNonZero(result.depth) << ',' << accepted;
      for(auto v:pose.p) profile << ',' << v;for(auto v:pose.q) profile << ',' << v;profile << '\n';
    }
    if(++frames%100==0) std::cerr << "vio-depth: frames=" << frames << " tracked=" << tracked << " fused=" << fused
      << " pair_hz=" << (frames-1)*1e9/(last-first) << " skipped=" << skipped << " mean_join_ms=" << total_wall/frames << '\n';
  }
  live_imu.stop();int status=camera.stop();live_imu.report();camera.report();
  std::cerr << "vio-depth final: frames=" << frames << " poses=" << tracked << " fused=" << fused << " queued=" << queued
    << " pair_hz=" << (frames>1?(frames-1)*1e9/(last-first):0) << " skipped=" << skipped << " over_50ms=" << overruns
    << " mean_join_ms=" << (frames?total_wall/frames:0) << '\n';
  return status;
} catch(const std::exception& e) { std::cerr << "vio-depth: " << e.what() << '\n';return 1; }

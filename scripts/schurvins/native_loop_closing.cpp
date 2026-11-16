// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_loop_closing.hpp"
#include <svo/online_loopclosing/loop_closing.h>
#include <svo/online_loopclosing/map_alignment.h>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <sys/resource.h>
extern "C" void tidesense_orb_profile_report() __attribute__((weak));
extern "C" void tidesense_orb_ive_report() __attribute__((weak));
extern "C" void tidesense_orb_blur_report() __attribute__((weak));
#ifdef SCHUR_LOOP_OFFLOAD
#include "loop-closing/offload/matcher.hpp"
#endif

namespace {
template<class T> T value(const YAML::Node& n, const char* key, T fallback) {
  return n && n[key] ? n[key].as<T>() : fallback;
}
bool flag(const char* name, bool fallback=false) {
  const char* text=std::getenv(name);
  if (!text) return fallback;
  if (std::string(text)=="1") return true;
  if (std::string(text)=="0") return false;
  throw std::runtime_error(std::string(name)+" must be 0 or 1");
}
svo::LoopClosureOptions options(const YAML::Node& config) {
  const auto n = config["loop_closing"];
  svo::LoopClosureOptions o{};
  o.runlc = true;
  const char* voc = std::getenv("SCHUR_LOOP_VOC");
  o.voc_name = voc ? voc : value<std::string>(n,"vocabulary","");
  if (o.voc_name.empty() || !std::ifstream(o.voc_name))
    throw std::runtime_error("SCHUR_LOOP_VOC must name a readable DBoW2 ORB vocabulary");
  o.voc_path = "";
  // Match the shipped upstream stereo YAML, whose overlap thresholds are fractions.
  o.alpha = value<double>(n,"alpha",0.8);
  o.beta = value<double>(n,"beta",0.8);
  o.ignored_past_frames = value<int>(n,"frames_to_ignore",20);
  o.scale_ret_app = "None";
  o.global_map_type = "None";
  o.bowthresh = value<double>(n,"bow_thresh",0.65);
  o.gv_3d_inlier_thresh = value<double>(n,"gv_3d_inlier_thresh",0.4);
  o.min_num_3d = value<int>(n,"min_num_3d",10);
  o.orb_dist_thresh = value<int>(n,"orb_dist_thresh",48);
  o.gv_2d_match_thresh = value<double>(n,"gv_2d_match_thresh",0.10);
  o.use_opengv = false;
  o.enable_image_logging = false;
  o.reuse_orb = flag("SCHUR_LOOP_REUSE_ORB");
  o.precheck_3d = flag("SCHUR_LOOP_PRECHECK_3D");
  o.proximity_dist_ratio = value<double>(n,"proximity_dist_ratio",0.01);
  o.proximity_offset = value<double>(n,"proximity_offset",0.1);
  // No EKF correction is applied, so do not suspend detection awaiting one.
  o.force_correction_dist_thresh_meter = std::numeric_limits<double>::infinity();
  if (!(o.alpha >= 0 && o.alpha <= 1) || !(o.beta >= 0 && o.beta <= 1) ||
      o.ignored_past_frames < 0 || o.min_num_3d < 3 || o.orb_dist_thresh < 1 ||
      !(o.bowthresh >= 0 && o.bowthresh <= 1) ||
      !(o.gv_3d_inlier_thresh >= 0 && o.gv_3d_inlier_thresh <= 1) ||
      !(o.gv_2d_match_thresh >= 0 && o.gv_2d_match_thresh <= 1) ||
      !(o.proximity_dist_ratio >= 0) || !(o.proximity_offset >= 0))
    throw std::runtime_error("Invalid loop_closing thresholds");
  return o;
}
}

struct NativeLoopClosing::Impl {
  svo::LoopClosing detector;
  std::thread worker;
  std::mutex mutex;
  std::condition_variable ready;
  svo::KeyFramePtr pending;
  bool stopping = false, finished = false;
  std::exception_ptr error;
  std::ofstream csv;
  size_t received = 0, submitted = 0, dropped = 0, processed = 0;
  size_t max_keyframes;
  int worker_nice = -1;  // Inherit unless the experiment explicitly sets a value.
  double work_ms = 0, max_work_ms = 0;
#ifdef SCHUR_LOOP_OFFLOAD
  std::unique_ptr<LoopMatchProvider> matcher;
#endif

  Impl(const svo::CameraBundle::Ptr& cameras, const YAML::Node& config)
    : detector(options(config), cameras),
      max_keyframes(value<size_t>(config["loop_closing"],"max_keyframes",500)) {
    if(const char* mode=std::getenv("SCHUR_ORB_BLUR")) {
      if(std::string(mode)!="cpu" && std::string(mode)!="tpu-q9")
        throw std::runtime_error("SCHUR_ORB_BLUR must be cpu or tpu-q9");
#ifndef SCHUR_LOOP_TPU_GAUSSIAN
      if(std::string(mode)=="tpu-q9")throw std::runtime_error("Build with SCHUR_LOOP_TPU_GAUSSIAN=ON for Gaussian offload");
#endif
    }
    if (const char* text=std::getenv("SCHUR_LOOP_NICE")) {
      size_t end=0;worker_nice=std::stoi(text,&end);
      if (text[end] || worker_nice<0 || worker_nice>19)
        throw std::runtime_error("SCHUR_LOOP_NICE must be 0..19");
    }
    if(const char* kind=std::getenv("SCHUR_ORB_DETECTOR")) {
      if(std::string(kind)!="fast" && std::string(kind)!="ive-st")
        throw std::runtime_error("SCHUR_ORB_DETECTOR must be fast or ive-st");
#ifndef SCHUR_LOOP_IVE_ORB
      if(std::string(kind)=="ive-st")throw std::runtime_error("Build with SCHUR_LOOP_IVE_ORB=ON for IVE ORB candidates");
#endif
    }
    if (max_keyframes < 20 || max_keyframes > 5000)
      throw std::runtime_error("loop_closing.max_keyframes must be 20..5000");
    svo::MapAlignmentOptions alignment;
    alignment.ransac3d_min_pts = 8;
    alignment.ransac3d_inlier_percent = 40.0;
    detector.map_alignment_se3_ = std::make_shared<svo::MapAlignmentSE3>(alignment);
    if (const char* path = std::getenv("SCHUR_LOOP_CSV")) {
      csv.open(path);
      if (!csv) throw std::runtime_error("Cannot open SCHUR_LOOP_CSV");
      csv << "frame_id,timestamp_s,history,admitted,queries,bow_candidates,geometry_checks,geometric_candidates,verified,orb_ms,bow_ms,matching_ms,geometry_ms,work_ms,reused_orb,precheck_3d_rejects\n";
    }
    std::cerr << "loop_closing: enabled ORB/DBoW2 + 2D/3D verification; detection only; "
      "single worker, one pending snapshot; max_keyframes=" << max_keyframes
      << " reuse_orb=" << detector.options_.reuse_orb << " precheck_3d=" << detector.options_.precheck_3d << '\n';
    worker = std::thread([this] { run(); });
  }
  ~Impl() { stop(); }
  void stop() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
    ready.notify_one();
    if (worker.joinable()) worker.join();
  }
  void run() noexcept {
    try {
      // Linux nice values apply per thread: keep camera/VIO threads unchanged.
      if (worker_nice>=0 && setpriority(PRIO_PROCESS,0,worker_nice))
        throw std::runtime_error("Cannot set loop worker nice value");
      errno=0;const int actual_nice=getpriority(PRIO_PROCESS,0);
      if (errno || (worker_nice>=0 && actual_nice!=worker_nice))
        throw std::runtime_error("Cannot verify loop worker nice value");
      std::cout<<"loop_worker: nice="<<actual_nice<<'\n';
      const char* mode=std::getenv("SCHUR_LOOP_MATCH_BACKEND");
      if(mode && std::string(mode)!="cpu") {
#ifdef SCHUR_LOOP_OFFLOAD
        const char* verify=std::getenv("SCHUR_LOOP_MATCH_VERIFY");
        if(verify && std::string(verify)!="0" && std::string(verify)!="1")
          throw std::runtime_error("SCHUR_LOOP_MATCH_VERIFY must be 0 or 1");
        matcher.reset(new LoopMatchProvider(mode,verify && std::string(verify)=="1"));
        svo::setFeatureMatchingAccelerator(matcher.get());
#else
        throw std::runtime_error("Build with SCHUR_LOOP_OFFLOAD=ON for descriptor offload");
#endif
      }
      for (;;) {
        svo::KeyFramePtr frame;
        {
          std::unique_lock<std::mutex> lock(mutex);
          ready.wait(lock,[&] { return stopping || pending; });
          if (!pending) break;
          frame.swap(pending);
        }
        if (detector.kf_list_.size() >= max_keyframes)
          throw std::runtime_error("Loop map reached max_keyframes; end this bounded experiment or configure a larger budget");
        const auto begin = std::chrono::steady_clock::now();
        const size_t history = detector.kf_list_.size();
        const int queries = std::accumulate(detector.num_queries_.begin(),detector.num_queries_.end(),0);
        const size_t bow = detector.num_bow_candidates_, geometry = detector.loop_detect_viz_info_.size();
        const size_t checks = detector.num_geometry_checks_;
        const size_t reused=detector.reused_orb_,prechecks=detector.num_3d_precheck_rejects_;
        const size_t verified = detector.lc_closed_loops_.size();
        const double orb_ms=detector.orb_ms_,bow_ms=detector.bow_ms_,matching_ms=detector.matching_ms_,geometry_ms=detector.geometry_ms_;
        // This thread exclusively owns all detector state. VIO never mutates the snapshot.
        detector.addKeyframeToPR(frame,true);
        frame->keyframe_image_.release();
        const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        ++processed; work_ms += ms; max_work_ms = std::max(max_work_ms,ms);
        if (csv.is_open()) {
          csv << frame->NframeID_ << ',' << std::setprecision(15) << frame->timestamp_sec_abs_ << ','
            << detector.kf_list_.size() << ',' << (detector.kf_list_.size()>history) << ','
            << std::accumulate(detector.num_queries_.begin(),detector.num_queries_.end(),0)-queries << ','
            << detector.num_bow_candidates_-bow << ',' << detector.num_geometry_checks_-checks << ','
            << detector.loop_detect_viz_info_.size()-geometry << ','
            << detector.lc_closed_loops_.size()-verified << ',' << detector.orb_ms_-orb_ms << ','
            << detector.bow_ms_-bow_ms << ',' << detector.matching_ms_-matching_ms << ','
            << detector.geometry_ms_-geometry_ms << ',' << ms << ','
            << detector.reused_orb_-reused << ',' << detector.num_3d_precheck_rejects_-prechecks << '\n';
          csv.flush();
          if (!csv) throw std::runtime_error("Loop CSV write failed");
        }
      }
      if(tidesense_orb_profile_report)tidesense_orb_profile_report();
      if(tidesense_orb_ive_report)tidesense_orb_ive_report();
      if(tidesense_orb_blur_report)tidesense_orb_blur_report();
      svo::setFeatureMatchingAccelerator(nullptr);
    } catch (...) {
      svo::setFeatureMatchingAccelerator(nullptr);
      std::lock_guard<std::mutex> lock(mutex);
      error = std::current_exception();
      stopping = true;
    }
  }
};

NativeLoopClosing::NativeLoopClosing(const svo::CameraBundle::Ptr& cameras, const YAML::Node& config)
  : impl_(new Impl(cameras,config)) {}
NativeLoopClosing::~NativeLoopClosing() = default;
void NativeLoopClosing::check() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->error) std::rethrow_exception(impl_->error);
}
void NativeLoopClosing::submit(const svo::FramePtr& frame) {
  check();
  if (!frame->isKeyframe()) return;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->received;
    if (impl_->pending) { ++impl_->dropped; return; }
    if (impl_->stopping) throw std::runtime_error("Loop detector has stopped");
  }
  auto snapshot = std::make_shared<svo::KeyFrame>(frame->bundleId());
  impl_->detector.svoFrameToKeyframe(frame,snapshot.get());
  snapshot->keyframe_image_ = frame->img().clone();
  // A Schur snapshot is not subject to the Ceres backend's scale-stability flag.
  snapshot->skip_frame_ = false;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->pending = std::move(snapshot);
    ++impl_->submitted;
  }
  impl_->ready.notify_one();
}
void NativeLoopClosing::finish() {
  if (impl_->finished) { check(); return; }
  impl_->stop();
  impl_->finished = true;
  check();
  const auto& d = impl_->detector;
  std::cout << "loop_closing: received=" << impl_->received << " submitted=" << impl_->submitted
    << " processed=" << impl_->processed << " dropped=" << impl_->dropped << " history=" << d.kf_list_.size()
    << " queries=" << std::accumulate(d.num_queries_.begin(),d.num_queries_.end(),0)
    << " bow_candidates=" << d.num_bow_candidates_ << " geometric_candidates=" << d.loop_detect_viz_info_.size()
    << " geometry_checks=" << d.num_geometry_checks_
    << " verified=" << d.lc_closed_loops_.size() << " work_ms=" << impl_->work_ms
    << " max_work_ms=" << impl_->max_work_ms << " orb_ms=" << d.orb_ms_ << " bow_ms=" << d.bow_ms_
    << " matching_ms=" << d.matching_ms_ << " geometry_ms=" << d.geometry_ms_
    << " reused_orb=" << d.reused_orb_ << " precheck_3d_rejects=" << d.num_3d_precheck_rejects_ << '\n';
#ifdef SCHUR_LOOP_OFFLOAD
  if(impl_->matcher) impl_->matcher->report();
#endif
}

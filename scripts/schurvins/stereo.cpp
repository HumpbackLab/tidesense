// SPDX-License-Identifier: GPL-3.0-or-later
// Native stereo + IMU entry point for the upstream SchurVINS pipeline.
#include "live_camera.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <time.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <glog/logging.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>
#include <svo/frame_handler_stereo.h>
#include <svo/imu_handler.h>
#include <svo/initialization.h>
#include <svo/reprojector.h>
#include <svo/stereo_triangulation.h>
#include <svo/direct/depth_filter.h>
#include <svo/direct/feature_detection.h>
#include <svo/tracker/feature_tracker.h>
#include <svo/common/frame.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
uint64_t clockNs(clockid_t clock) {
  timespec ts;
  if (clock_gettime(clock, &ts)) throw std::runtime_error("clock_gettime failed");
  return uint64_t(ts.tv_sec)*1000000000ULL + ts.tv_nsec;
}
std::vector<std::string> row(std::istream& in) {
  std::string s;
  while (std::getline(in, s)) {
    if (s.empty() || s[0] == '#') continue;
    std::vector<std::string> out;
    std::istringstream line(s);
    while (std::getline(line, s, ',')) out.push_back(s);
    return out;
  }
  return {};
}
uint64_t stamp(const std::string& s) {
  size_t n;
  if (s.empty() || s[0] == '-') throw std::runtime_error("Invalid timestamp");
  auto t = std::stoull(s, &n);
  if (n != s.size()) throw std::runtime_error("Timestamps must be integer nanoseconds");
  return t;
}
double number(const std::string& s) {
  size_t n;
  double v = std::stod(s, &n);
  if (n != s.size() || !std::isfinite(v)) throw std::runtime_error("Invalid IMU number");
  return v;
}
std::string path(const std::string& csv, const std::string& name) {
  if (!name.empty() && name[0] == '/') return name;
  auto pos = csv.find_last_of('/');
  return (pos == std::string::npos ? "" : csv.substr(0, pos + 1)) + name;
}
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "SchurVINS stereo + IMU (A53, no ROS)\n"
      "  schurvins_stereo CALIB.yaml STEREO.csv IMU.csv POSES.csv\n"
      "  schurvins_stereo --self-test SYNTHETIC_CALIB.yaml\n"
      "  schurvins_stereo --camera CALIB.yaml SENSOR.ini POSES.csv [FRAMES]\n"
      "  schurvins_stereo --camera-smoke SENSOR.ini [FRAMES]\n"
      "Camera modes run the stereo visual frontend without IMU. FRAMES defaults to 120; 0 runs until Ctrl-C.\n"
      "Camera smoke uses test-only geometry, reports input validation, and writes no poses.\n"
      "STEREO: timestamp_ns,left_image,right_image (paths relative to CSV)\n"
      "IMU: timestamp_ns,gx,gy,gz,ax,ay,az (rad/s, m/s^2)\n"
      "Rows must be strictly time-ordered; comments start with #.\n";
    return 0;
  }
  bool self = argc == 3 && std::string(argv[1]) == "--self-test";
  bool smoke = argc >= 2 && std::string(argv[1]) == "--camera-smoke";
  bool camera = smoke || (argc >= 2 && std::string(argv[1]) == "--camera");
  if ((smoke && argc != 3 && argc != 4) ||
      (camera && !smoke && argc != 5 && argc != 6) ||
      (!camera && !self && argc != 5)) { std::cerr << "Use --help for usage\n"; return 2; }
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  cv::setNumThreads(1);
  try {
    const uint64_t frame_limit = camera ? ((smoke ? argc == 4 : argc == 6) ?
      stamp(argv[smoke ? 3 : 5]) : 120) : 0;
    const std::string calibration = smoke ? executableDirectory() + "/../camera-smoke.yaml" : argv[(self || camera) ? 2 : 1];
    auto yaml = YAML::LoadFile(calibration);
    if (!camera && (!yaml["imu_params"] || !yaml["imu_initialization"]))
      throw std::runtime_error("Calibration must include imu_params and imu_initialization");
    auto cams = svo::CameraBundle::loadFromYaml(calibration);
    if (!cams || cams->numCameras() != 2) throw std::runtime_error("Two cameras required");
    if (camera && !smoke && ((yaml["test_only"] && yaml["test_only"].as<bool>()) ||
        (yaml["label"] && yaml["label"].as<std::string>().find("SYNTHETIC") != std::string::npos)))
      throw std::runtime_error("Use real calibration for --camera; test geometry is only allowed in --camera-smoke");
    if (camera) for (size_t c = 0; c < 2; ++c)
      if (cams->getCamera(c).imageWidth() != 640 || cams->getCamera(c).imageHeight() != 400)
        throw std::runtime_error("Live camera calibration must be 640x400");
    svo::ImuCalibration imu_cal;
    std::shared_ptr<svo::ImuHandler> imu;
    if (!camera) {
      imu_cal = svo::ImuHandler::loadCalibrationFromFile(calibration);
      if (imu_cal.delay_imu_cam != 0.0)
        throw std::runtime_error("Pre-align IMU/camera timestamps and set delay_imu_cam to zero");
      auto imu_init = svo::ImuHandler::loadInitializationFromFile(calibration);
      imu = std::make_shared<svo::ImuHandler>(imu_cal, imu_init, svo::IMUHandlerOptions());
    }
    svo::BaseOptions base;
    base.use_imu = !camera;
    base.max_n_kfs = 5;
    base.window_size_ = 4;
    base.quality_min_fts = 20;
    base.img_align_max_level = 3;
    base.img_align_min_level = 1;
    base.trace_statistics = false;
    svo::DetectorOptions detector;
    detector.cell_size = 25;
    detector.detector_type = svo::DetectorType::kFast;
    detector.threshold_primary = 10;
    svo::ReprojectorOptions reproj;
    reproj.cell_size = detector.cell_size;
    reproj.max_n_features_per_frame = 120;
    svo::DepthFilterOptions depth;
    depth.use_threaded_depthfilter = false;
    depth.max_n_seeds_per_frame = 240;
    svo::InitializationOptions init;
    init.init_type = svo::InitializerType::kStereo;
    init.init_min_features = 20;
    svo::FrameHandlerStereo vio(base, depth, detector, init,
        svo::StereoTriangulationOptions(), reproj, svo::FeatureTrackerOptions(), cams);
    if (imu) vio.setImuHandler(imu);
    vio.start();
    std::ifstream images, inertial;
    std::ofstream poses;
    if (!self && !camera) {
      images.open(argv[2]); inertial.open(argv[3]); poses.open(argv[4]);
      if (!images || !inertial || !poses) throw std::runtime_error("Cannot open input/output files");
      poses << "#timestamp_ns,px,py,pz,qx,qy,qz,qw\n";
    }
    if (camera && !smoke) {
      poses.open(argv[4]);
      if (!poses) throw std::runtime_error("Cannot open pose output");
      poses << "# visual-only, no IMU; timestamp is left VI arrival PTS, not calibrated exposure time\n"
            << "#timestamp_ns,px,py,pz,qx,qy,qz,qw\n";
    }
    std::unique_ptr<LiveCamera> live;
    if (camera) {
      struct sigaction sa{}; sa.sa_handler = interrupt;
      sigaction(SIGINT, &sa, nullptr); sigaction(SIGTERM, &sa, nullptr);
      const char* helper = std::getenv("SCHUR_CAMERA_HELPER");
      live.reset(new LiveCamera(helper ? helper : executableDirectory() + "/schurvins_camera", argv[smoke ? 2 : 3]));
      std::cout << "Live stereo visual frontend, IMU disabled; timestamps are VI arrival PTS\n";
    }
    std::ofstream timings;
    if (const char* timing_path = std::getenv("SCHUR_TIMING_CSV")) {
      timings.open(timing_path);
      if (!timings) throw std::runtime_error("Cannot open SCHUR_TIMING_CSV");
      timings << "frame,timestamp_ns,vio_ms,tracking\n";
    }
    std::ofstream profile;
    if (const char* profile_path = std::getenv("SCHUR_PROFILE_CSV")) {
      profile.open(profile_path);
      if (!profile) throw std::runtime_error("Cannot open SCHUR_PROFILE_CSV");
      profile << "frame,timestamp_ns,begin_mono_ns,end_mono_ns,cpu_ns,tracking\n";
    }
    std::vector<std::string> pending;
    uint64_t last_imu = 0, last_image = 0;
    size_t frames = 0, tracked = 0, fused = 0;
    cv::Mat synthetic;
    if (self) {
      const auto& c = cams->getCamera(0);
      synthetic.create(c.imageHeight(), c.imageWidth(), CV_8UC1);
      cv::RNG rng(42); rng.fill(synthetic, cv::RNG::UNIFORM, 0, 256);
      cv::GaussianBlur(synthetic, synthetic, cv::Size(3,3), 0.8);
      for (uint64_t t = 100000000; t <= 995000000; t += 5000000) {
        imu->addImuMeasurement(svo::ImuMeasurement(t * 1e-9, Eigen::Vector3d::Zero(), Eigen::Vector3d(0,0,imu_cal.gravity_magnitude)));
        last_imu = t;
      }
    }
    uint64_t test_delay_ms = 0;
    if (camera && std::getenv("SCHUR_TEST_PROCESS_DELAY_MS")) {
      test_delay_ms = stamp(std::getenv("SCHUR_TEST_PROCESS_DELAY_MS"));
      if (test_delay_ms > 1000) throw std::runtime_error("Test processing delay must be <=1000ms");
    }
    double max_age_ms = 0;
    uint64_t first_live_stamp = 0, last_live_stamp = 0;
    while (true) {
      uint64_t t;
      std::vector<cv::Mat> pair;
      if (camera) {
        if (frame_limit && frames >= frame_limit) break;
        LivePair input;
        if (!live->next(input, interrupted)) break;
        t = input.header.left_ns;
        if (!first_live_stamp) first_live_stamp = t;
        last_live_stamp = t;
        if (t <= last_image) throw std::runtime_error("Live timestamps not increasing");
        last_image = t;
        const uint64_t now = clockNs(CLOCK_MONOTONIC);
        if (now < t) throw std::runtime_error("VI PTS is not in the expected monotonic clock domain");
        max_age_ms = std::max(max_age_ms, (now-t)*1e-6);
        pair = {input.left, input.right};
        // Optional consumer-delay injection for bounded-queue board testing.
        if (test_delay_ms)
          std::this_thread::sleep_for(std::chrono::milliseconds(test_delay_ms));
      } else if (self) {
        if (frames == 6) break;
        t = 1000000000ULL + frames * 50000000ULL;
        cv::Mat right = cv::Mat::zeros(synthetic.size(), CV_8UC1);
        synthetic.colRange(8, synthetic.cols).copyTo(right.colRange(0, right.cols-8));
        pair = {synthetic, right};
        while (last_imu <= t) {
          last_imu += 5000000;
          imu->addImuMeasurement(svo::ImuMeasurement(last_imu * 1e-9, Eigen::Vector3d::Zero(), Eigen::Vector3d(0,0,imu_cal.gravity_magnitude)));
        }
      } else {
        auto r = row(images);
        if (r.empty()) break;
        if (r.size() != 3) throw std::runtime_error("Expected 3 columns in stereo CSV");
        t = stamp(r[0]);
        if (t <= last_image) throw std::runtime_error("Image timestamps not increasing");
        last_image = t;
        while (last_imu <= t) {
          pending = row(inertial);
          if (pending.size() != 7) throw std::runtime_error("IMU ended early or row does not have 7 columns");
          uint64_t it = stamp(pending[0]);
          if (it <= last_imu) throw std::runtime_error("IMU timestamps not increasing");
          if (last_imu == 0 && it >= t) throw std::runtime_error("IMU must start before first image");
          last_imu = it;
          imu->addImuMeasurement(svo::ImuMeasurement(it * 1e-9,
            Eigen::Vector3d(number(pending[1]), number(pending[2]), number(pending[3])),
            Eigen::Vector3d(number(pending[4]), number(pending[5]), number(pending[6]))));
        }
        pair = {cv::imread(path(argv[2],r[1]),cv::IMREAD_GRAYSCALE),
                cv::imread(path(argv[2],r[2]),cv::IMREAD_GRAYSCALE)};
      }
      for (size_t c = 0; c < 2; ++c) {
        if (pair[c].empty() || pair[c].cols != cams->getCamera(c).imageWidth() ||
            pair[c].rows != cams->getCamera(c).imageHeight())
          throw std::runtime_error("Image missing or dimensions disagree with calibration");
      }
      svo::Quaternion rotation;
      if (imu && !vio.hasStarted()) {
        if (!imu->getInitialAttitude(t * 1e-9, rotation)) {
          ++frames;
          continue;
        }
        vio.setRotationPrior(rotation);
      } else if (imu && vio.getLastFrames() && imu->getRelativeRotationPrior(
          vio.getLastFrames()->getMinTimestampSeconds(), t * 1e-9, false, rotation)) {
        vio.setRotationIncrementPrior(rotation);
      }
      const uint64_t cpu_begin = profile.is_open() ? clockNs(CLOCK_PROCESS_CPUTIME_ID) : 0;
      const uint64_t mono_begin = profile.is_open() ? clockNs(CLOCK_MONOTONIC) : 0;
      const auto vio_begin = std::chrono::steady_clock::now();
      vio.addImageBundle(pair, t);
      const auto vio_end = std::chrono::steady_clock::now();
      const uint64_t mono_end = profile.is_open() ? clockNs(CLOCK_MONOTONIC) : 0;
      const uint64_t cpu_end = profile.is_open() ? clockNs(CLOCK_PROCESS_CPUTIME_ID) : 0;
      if (profile.is_open()) profile << frames << ',' << t << ',' << mono_begin << ',' << mono_end
        << ',' << cpu_end-cpu_begin << ',' << (vio.stage() == svo::Stage::kTracking) << '\n';
      if (timings.is_open()) timings << frames << ',' << t << ',' << std::setprecision(12)
        << std::chrono::duration<double,std::milli>(vio_end-vio_begin).count() << ','
        << (vio.stage() == svo::Stage::kTracking) << '\n';
      ++frames;
      auto f = vio.getLastFrames();
      if (f && f->imu_ready_) ++fused;
      if (!f || vio.stage() != svo::Stage::kTracking) continue;
      auto T = f->get_T_W_B();
      const auto p = T.getPosition(); const auto q = T.getEigenQuaternion();
      if (!p.allFinite() || !q.coeffs().allFinite()) throw std::runtime_error("Non-finite pose");
      ++tracked;
      if (poses.is_open()) poses << t << std::setprecision(12) << ',' << p.x() << ',' << p.y() << ',' << p.z()
        << ',' << q.x() << ',' << q.y() << ',' << q.z() << ',' << q.w() << '\n';
    }
    std::cout << "frames=" << frames << " tracking=" << tracked << " imu_fused=" << fused << '\n';
    if (camera) {
      if (live->stop()) throw std::runtime_error("Camera helper exited with an error during cleanup");
      live->report();
      std::cout << "frontend_frames=" << frames << " max_input_age_ms=" << max_age_ms
                << " image_span_s=" << (last_live_stamp-first_live_stamp)*1e-9 << '\n';
      if (!frames) throw std::runtime_error("No camera images reached visual frontend");
      if (smoke) {
        std::cout << "CAMERA INPUT PASS (test geometry; no IMU, no VIO accuracy claim)\n";
        return 0;
      }
    }
    if (tracked == 0 || (!camera && fused == 0) || (self && (tracked != 6 || fused != 6)))
      throw std::runtime_error("Stereo/IMU pipeline did not track expected frames");
    std::cout << (self ? "SELF-TEST PASS (synthetic data; not a VIO accuracy test)\n" : "Completed\n");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n'; return 1;
  }
}

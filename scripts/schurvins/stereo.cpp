// SPDX-License-Identifier: GPL-3.0-or-later
// Native stereo + IMU entry point for the upstream SchurVINS pipeline.
#include <algorithm>
#include <cmath>
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
      "STEREO: timestamp_ns,left_image,right_image (paths relative to CSV)\n"
      "IMU: timestamp_ns,gx,gy,gz,ax,ay,az (rad/s, m/s^2)\n"
      "Rows must be strictly time-ordered; comments start with #.\n";
    return 0;
  }
  bool self = argc == 3 && std::string(argv[1]) == "--self-test";
  if (!self && argc != 5) { std::cerr << "Use --help for usage\n"; return 2; }
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  cv::setNumThreads(1);
  try {
    const std::string calibration = argv[self ? 2 : 1];
    auto yaml = YAML::LoadFile(calibration);
    if (!yaml["imu_params"] || !yaml["imu_initialization"])
      throw std::runtime_error("Calibration must include imu_params and imu_initialization");
    auto cams = svo::CameraBundle::loadFromYaml(calibration);
    if (!cams || cams->numCameras() != 2) throw std::runtime_error("Two cameras required");
    auto imu_cal = svo::ImuHandler::loadCalibrationFromFile(calibration);
    if (imu_cal.delay_imu_cam != 0.0)
      throw std::runtime_error("Pre-align IMU/camera timestamps and set delay_imu_cam to zero");
    auto imu_init = svo::ImuHandler::loadInitializationFromFile(calibration);
    auto imu = std::make_shared<svo::ImuHandler>(imu_cal, imu_init, svo::IMUHandlerOptions());
    svo::BaseOptions base;
    base.use_imu = true;
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
    vio.setImuHandler(imu);
    vio.start();
    std::ifstream images, inertial;
    std::ofstream poses;
    if (!self) {
      images.open(argv[2]); inertial.open(argv[3]); poses.open(argv[4]);
      if (!images || !inertial || !poses) throw std::runtime_error("Cannot open input/output files");
      poses << "#timestamp_ns,px,py,pz,qx,qy,qz,qw\n";
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
    while (true) {
      uint64_t t;
      std::vector<cv::Mat> pair;
      if (self) {
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
      if (!vio.hasStarted()) {
        if (!imu->getInitialAttitude(t * 1e-9, rotation)) {
          ++frames;
          continue;
        }
        vio.setRotationPrior(rotation);
      } else if (vio.getLastFrames() && imu->getRelativeRotationPrior(
          vio.getLastFrames()->getMinTimestampSeconds(), t * 1e-9, false, rotation)) {
        vio.setRotationIncrementPrior(rotation);
      }
      vio.addImageBundle(pair, t);
      ++frames;
      auto f = vio.getLastFrames();
      if (f && f->imu_ready_) ++fused;
      if (!f || vio.stage() != svo::Stage::kTracking) continue;
      auto T = f->get_T_W_B();
      const auto p = T.getPosition(); const auto q = T.getEigenQuaternion();
      if (!p.allFinite() || !q.coeffs().allFinite()) throw std::runtime_error("Non-finite pose");
      ++tracked;
      if (!self) poses << t << std::setprecision(12) << ',' << p.x() << ',' << p.y() << ',' << p.z()
        << ',' << q.x() << ',' << q.y() << ',' << q.z() << ',' << q.w() << '\n';
    }
    std::cout << "frames=" << frames << " tracking=" << tracked << " imu_fused=" << fused << '\n';
    if (tracked == 0 || fused == 0 || (self && (tracked != 6 || fused != 6)))
      throw std::runtime_error("Stereo/IMU pipeline did not track expected frames");
    std::cout << (self ? "SELF-TEST PASS (synthetic data; not a VIO accuracy test)\n" : "Completed\n");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n'; return 1;
  }
}

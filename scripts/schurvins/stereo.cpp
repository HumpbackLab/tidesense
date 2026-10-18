// SPDX-License-Identifier: GPL-3.0-or-later
// Native stereo + IMU entry point for the upstream SchurVINS pipeline.
#include "live_camera.hpp"
#include "live_imu.hpp"
#include "imu_prediction.hpp"
#include <svo/schur_vins.h>
#include "micro_ros_bridge.hpp"
#include "calibration.hpp"
#include "gyro_calibration.hpp"
#include "recording.hpp"
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
  bool allow_incomplete = argc == 5 && std::string(argv[1]) == "--replay"
    && std::string(argv[4]) == "--allow-incomplete";
  if (allow_incomplete) --argc;
  std::string record_dir;
  if (argc >= 3 && std::string(argv[argc-2]) == "--record") {
    if (std::string(argv[1]) != "--vio" || !argv[argc-1][0]) {
      std::cerr << "--record NEW_DIR is only supported as the final arguments of --vio\n"; return 2;
    }
    record_dir = argv[argc-1]; argc -= 2;
  }
  std::string replay_calibration, replay_images, replay_imu;
  char* replay_args[5];
  if (argc == 4 && std::string(argv[1]) == "--replay") {
    const std::string dir = argv[2];
    const bool incomplete = !std::ifstream(dir + "/COMPLETE") || bool(std::ifstream(dir + "/INCOMPLETE"));
    if (incomplete && !allow_incomplete) {
      std::cerr << "Replay requires a completed recording (COMPLETE marker). "
                << "Use --allow-incomplete explicitly to replay its committed prefix.\n";
      return 2;
    }
    if (incomplete) std::cerr << "WARNING: replaying incomplete recording; only committed stereo.csv rows are used.\n";
    replay_calibration = dir + "/replay.yaml";
    replay_images = dir + "/stereo.csv";
    replay_imu = dir + "/imu.csv";
    replay_args[0] = argv[0]; replay_args[1] = &replay_calibration[0];
    replay_args[2] = &replay_images[0]; replay_args[3] = &replay_imu[0];
    replay_args[4] = argv[3]; argv = replay_args; argc = 5;
  }
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "SchurVINS stereo + IMU (A53, optional micro-ROS)\n"
      "  schurvins_stereo CALIB.yaml STEREO.csv IMU.csv POSES.csv\n"
      "  schurvins_stereo --calibrate SENSOR.ini --fps HZ [--frames N] [--timestamps CSV] [--imu auto|DEVICE] [--imu-timestamps CSV]\n"
      "    Independent FSIN stereo Image publishers; 0.1 <= HZ <= 20; default runs until Ctrl-C.\n"
      "  schurvins_stereo --self-test SYNTHETIC_CALIB.yaml\n"
      "  schurvins_stereo --camera CALIB.yaml SENSOR.ini POSES.csv [FRAMES]\n"
      "  schurvins_stereo --camera-smoke SENSOR.ini [FRAMES]\n"
      "  schurvins_stereo --vio CALIB.yaml SENSOR.ini IMU_DEVICE POSES.csv [FRAMES] [--record NEW_DIR]\n"
      "  schurvins_stereo --replay RECORD_DIR POSES.csv [--allow-incomplete]\n"
      "    Records consumed mono8 pairs + continuous IMU with input calibration snapshots.\n"
      "    Replay uses already aligned timestamps and corrected IMU; no hardware/ROS required.\n"
      "  schurvins_stereo --vio-smoke SENSOR.ini IMU_DEVICE [FRAMES]\n"
      "VIO modes acquire SC7U22 accel/gyro at 200Hz. Use run.sh with IMU_DEVICE=auto.\n"
      "IMU prediction defaults ON: IMU-rate micro-ROS poses; POSES.csv stays visual-rate.\n"
      "SCHUR_PREDICTION=0 disables prediction; SCHUR_PREDICTION_CSV=<path> saves predicted poses.\n"
      "Camera modes run the stereo visual frontend without IMU. FRAMES defaults to 120; 0 runs until Ctrl-C.\n"
      "Camera smoke uses test-only geometry, reports input validation, and writes no poses.\n"
      "STEREO: timestamp_ns,left_image,right_image (paths relative to CSV)\n"
      "IMU: timestamp_ns,gx,gy,gz,ax,ay,az (rad/s, m/s^2)\n"
      "Rows must be strictly time-ordered; comments start with #.\n"
      "micro-ROS runtime default OFF. Set SCHUR_MICRO_ROS=1 and SCHUR_MICRO_ROS_AGENT=<computer IPv4>.\n"
      "Optional SCHUR_MICRO_ROS_PORT=8888, SCHUR_MICRO_ROS_DOMAIN=0. SCHUR_MICRO_ROS=0 disables it.\n"
      "Live ROS stamps use system realtime mapped from acquisition monotonic time; CSV stamps are unchanged.\n"
      "micro-ROS compiled: " << (MicroRosBridge::compiled() ? "yes" : "no") << '\n';
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--calibrate") return runCalibration(argc, argv);
  bool self = argc == 3 && std::string(argv[1]) == "--self-test";
  const std::string mode = argc > 1 ? argv[1] : "";
  bool live_vio = mode == "--vio" || mode == "--vio-smoke";
  bool smoke = mode == "--camera-smoke" || mode == "--vio-smoke";
  bool camera = live_vio || smoke || mode == "--camera";
  int count_index = smoke ? (live_vio ? 4 : 3) : (live_vio ? 6 : 5);
  if ((camera && argc != count_index && argc != count_index+1) ||
      (!camera && !self && argc != 5)) { std::cerr << "Use --help for usage\n"; return 2; }
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = true;
  cv::setNumThreads(1);
  try {
    MicroRosBridge micro_ros;
    // Use acquisition time, not network arrival time. Dataset timestamps remain
    // unchanged. Live timestamps share CLOCK_MONOTONIC with the input adapters.
    const int64_t ros_clock_offset = camera && micro_ros.enabled()
      ? int64_t(clockNs(CLOCK_REALTIME)) - int64_t(clockNs(CLOCK_MONOTONIC)) : 0;
    const uint64_t frame_limit = camera ? (argc == count_index+1 ? stamp(argv[count_index]) : 120) : 0;
    const bool use_imu = !camera || live_vio;
    const std::string calibration = smoke ? executableDirectory() + "/../camera-smoke.yaml" : argv[(self || camera) ? 2 : 1];
    auto yaml = YAML::LoadFile(calibration);
    if (use_imu && (!yaml["imu_params"] || !yaml["imu_initialization"]))
      throw std::runtime_error("Calibration must include imu_params and imu_initialization");
    const auto gyro_calibration = GyroCalibration::parse(yaml["imu_params"]);
    if (live_vio && gyro_calibration.input_is_corrected)
      throw std::runtime_error("Live IIO gyro is nominal SI; gyro_input_is_corrected must be false");
    if (use_imu) {
      std::cerr << "imu: gyroscope matrix correction "
                << (gyro_calibration.applies() ? "enabled" :
                    gyro_calibration.input_is_corrected ? "already applied to input" : "not configured")
                << "; estimator and prediction share the same gyro input\n";
      if (gyro_calibration.configured) for (const auto& row : gyro_calibration.matrix)
        std::cerr << "imu: gyro_inverse_matrix " << std::setprecision(17)
                  << row[0] << ' ' << row[1] << ' ' << row[2] << '\n';
    }
    auto cams = svo::CameraBundle::loadFromYaml(calibration);
    if (!cams || cams->numCameras() != 2) throw std::runtime_error("Two cameras required");
    if (camera && !smoke && ((yaml["test_only"] && yaml["test_only"].as<bool>()) ||
        (yaml["label"] && yaml["label"].as<std::string>().find("SYNTHETIC") != std::string::npos)))
      throw std::runtime_error("Use real calibration for --camera/--vio; test geometry is only allowed in smoke modes");
    if (camera) for (size_t c = 0; c < 2; ++c)
      if (cams->getCamera(c).imageWidth() != 640 || cams->getCamera(c).imageHeight() != 400)
        throw std::runtime_error("Live camera calibration must be 640x400");
    svo::ImuCalibration imu_cal;
    std::shared_ptr<svo::ImuHandler> imu;
    if (use_imu) {
      imu_cal = svo::ImuHandler::loadCalibrationFromFile(calibration);
      if (imu_cal.delay_imu_cam != 0.0)
        throw std::runtime_error("Pre-align IMU/camera timestamps and set delay_imu_cam to zero");
      auto imu_init = svo::ImuHandler::loadInitializationFromFile(calibration);
      imu = std::make_shared<svo::ImuHandler>(imu_cal, imu_init, svo::IMUHandlerOptions());
    }
    if (live_vio && imu_cal.imu_rate != 200)
      throw std::runtime_error("Live SC7U22 requires calibration imu_rate: 200");
    int64_t camera_offset_ns = camera && yaml["live_camera_time_offset_ns"] ? yaml["live_camera_time_offset_ns"].as<int64_t>() : 0;
    if (camera_offset_ns < -1000000000LL || camera_offset_ns > 1000000000LL)
      throw std::runtime_error("live_camera_time_offset_ns must be within +/-1 second");
    svo::BaseOptions base;
    base.use_imu = use_imu;
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
    std::unique_ptr<VioRecording> recording;
    if (!record_dir.empty())
      recording.reset(new VioRecording(record_dir, yaml, argv[3]));
    std::ifstream images, inertial;
    std::ofstream poses;
    if (!self && !camera) {
      images.open(argv[2]); inertial.open(argv[3]); poses.open(argv[4]);
      if (!images || !inertial || !poses) throw std::runtime_error("Cannot open input/output files");
      poses << "#timestamp_ns,px,py,pz,qx,qy,qz,qw\n";
    }
    if (camera && !smoke) {
      poses.open(argv[live_vio ? 5 : 4]);
      if (!poses) throw std::runtime_error("Cannot open pose output");
      poses << (live_vio ? "# stereo + real 200Hz SC7U22 IMU; camera PTS offset applied\n" :
                           "# visual-only, no IMU; timestamp is left VI arrival PTS\n")
            << "#timestamp_ns,px,py,pz,qx,qy,qz,qw\n";
    }
    std::ofstream prediction_csv;
    uint64_t predicted_count = 0;
    std::unique_ptr<ImuPrediction> prediction;
    const char* health_option = std::getenv("SCHUR_HEALTH_GATE");
    if (health_option && std::string(health_option) != "0" &&
        std::string(health_option) != "1")
      throw std::runtime_error("SCHUR_HEALTH_GATE must be 0 or 1");
    const bool health_gate = health_option && std::string(health_option) == "1";
    const char* prediction_option = std::getenv("SCHUR_PREDICTION");
    if (prediction_option && std::string(prediction_option) != "0" && std::string(prediction_option) != "1")
      throw std::runtime_error("SCHUR_PREDICTION must be 0 or 1");
    const bool predict = use_imu && !self && !smoke && (!prediction_option || std::string(prediction_option) == "1");
    if (const char* csv = std::getenv("SCHUR_PREDICTION_CSV")) {
      if (!predict) throw std::runtime_error("SCHUR_PREDICTION_CSV requires IMU prediction");
      prediction_csv.open(csv);
      if (!prediction_csv) throw std::runtime_error("Cannot open prediction CSV");
      prediction_csv << "#timestamp_ns,px,py,pz,qx,qy,qz,qw,vx,vy,vz,tracking\n";
    }
    if (predict) {
      prediction.reset(new ImuPrediction([&](const PredictedState& s, bool valid) {
        const uint64_t stamp = uint64_t(int64_t(s.stamp_ns) + ros_clock_offset);
        micro_ros.submit(stamp, valid, valid ? s.p.data() : nullptr, valid ? s.q.coeffs().data() : nullptr);
        if (valid) ++predicted_count;
        if (prediction_csv.is_open()) {
          prediction_csv << s.stamp_ns << std::setprecision(15);
          for (int i=0;i<3;++i) prediction_csv << ',' << s.p[i];
          for (int i=0;i<4;++i) prediction_csv << ',' << s.q.coeffs()[i];
          for (int i=0;i<3;++i) prediction_csv << ',' << s.v[i];
          prediction_csv << ',' << valid << '\n';
          if (!prediction_csv) throw std::runtime_error("Prediction CSV write failed");
        }
      }, live_vio));
      std::cerr << "prediction: IMU-rate poses, visual correction + repropagation; stale after 500ms\n";
    }
    std::unique_ptr<LiveImu> live_imu;
    std::unique_ptr<LiveCamera> live;
    uint64_t live_ready_ns = 0, startup_pairs_skipped = 0;
    if (camera) {
      struct sigaction sa{}; sa.sa_handler = interrupt;
      sigaction(SIGINT, &sa, nullptr); sigaction(SIGTERM, &sa, nullptr);
      const char* helper = std::getenv("SCHUR_CAMERA_HELPER");
      live.reset(new LiveCamera(helper ? helper : executableDirectory() + "/schurvins_camera", argv[smoke ? 2 : 3]));
      if (live_vio) {
        // VI initialization can hold the single CPU in kernel work for >15ms.
        // Begin inertial history only after the camera has completed startup.
        LivePair warmup;
        if (!live->next(warmup,interrupted)) throw std::runtime_error("Interrupted during camera startup");
        ++startup_pairs_skipped;
        live_imu.reset(new LiveImu(argv[smoke ? 3 : 4], [&](const ImuSample& sample) {
          if (prediction) {
            auto corrected = sample;
            corrected.gyro = gyro_calibration.apply(sample.gyro);
            prediction->sample(corrected);
          }
        }));
        if (recording) recording->setImuMetadata(live_imu->recordingMetadata());
        live_ready_ns = clockNs(CLOCK_MONOTONIC)+100000000ULL;
      }
      std::cout << (live_vio ? "Live stereo + SC7U22 200Hz IMU\n" : "Live stereo visual frontend, IMU disabled\n")
                << "Camera timestamp offset_ns=" << camera_offset_ns << " (uncalibrated if zero)\n";
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
    size_t frames = 0, tracked = 0, fused = 0, imu_added = 0, max_imu_batch = 0;
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
        const uint64_t raw_stamp = input.header.left_ns;
        if (raw_stamp > uint64_t(INT64_MAX)-1000000000ULL ||
            (camera_offset_ns < 0 && raw_stamp <= uint64_t(-camera_offset_ns)))
          throw std::runtime_error("Camera timestamp offset out of range");
        t = uint64_t(int64_t(raw_stamp)+camera_offset_ns);
        if (live_imu && t < live_ready_ns) { ++startup_pairs_skipped; continue; }
        if (!first_live_stamp) first_live_stamp = t;
        last_live_stamp = t;
        if (t <= last_image) throw std::runtime_error("Live timestamps not increasing");
        last_image = t;
        const uint64_t now = clockNs(CLOCK_MONOTONIC);
        if (now < raw_stamp) throw std::runtime_error("VI PTS is not in the expected monotonic clock domain");
        max_age_ms = std::max(max_age_ms, (now-raw_stamp)*1e-6);
        pair = {input.left, input.right};
        if (live_imu) {
          auto samples = live_imu->through(t, interrupted);
          if (interrupted) break;
          max_imu_batch = std::max(max_imu_batch,samples.size());
          for (const auto& sample : samples) {
            if (sample.stamp_ns <= last_imu || (!last_imu && sample.stamp_ns >= t))
              throw std::runtime_error("IMU must be increasing and start before the first camera frame");
            last_imu = sample.stamp_ns;
            const auto gyro = gyro_calibration.apply(sample.gyro);
            imu->addImuMeasurement(svo::ImuMeasurement(last_imu*1e-9,
              Eigen::Vector3d(gyro[0],gyro[1],gyro[2]),
              Eigen::Vector3d(sample.accel[0],sample.accel[1],sample.accel[2])));
            ++imu_added;
          }
          if (last_imu <= t) throw std::runtime_error("IMU does not cover image interpolation edge");
          if (recording) recording->submit(t, input, samples);
        }
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
          ImuSample sample{}; sample.stamp_ns=it;
          for (int axis=0;axis<3;++axis) {
            sample.gyro[axis]=number(pending[axis+1]); sample.accel[axis]=number(pending[axis+4]);
          }
          sample.gyro = gyro_calibration.apply(sample.gyro);
          if (prediction) prediction->sample(sample);
          imu->addImuMeasurement(svo::ImuMeasurement(it * 1e-9,
            Eigen::Vector3d(sample.gyro[0],sample.gyro[1],sample.gyro[2]),
            Eigen::Vector3d(sample.accel[0],sample.accel[1],sample.accel[2])));
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
      if (prediction) prediction->check();
      vio.addImageBundle(pair, t);
      const auto vio_end = std::chrono::steady_clock::now();
      const uint64_t mono_end = profile.is_open() ? clockNs(CLOCK_MONOTONIC) : 0;
      const uint64_t cpu_end = profile.is_open() ? clockNs(CLOCK_PROCESS_CPUTIME_ID) : 0;
      const bool tracking_healthy = vio.stage() == svo::Stage::kTracking &&
        (!health_gate ||
         vio.trackingQuality() != svo::TrackingQuality::kInsufficient);
      if (profile.is_open()) profile << frames << ',' << t << ',' << mono_begin << ',' << mono_end
        << ',' << cpu_end-cpu_begin << ',' << tracking_healthy << '\n';
      if (timings.is_open()) timings << frames << ',' << t << ',' << std::setprecision(12)
        << std::chrono::duration<double,std::milli>(vio_end-vio_begin).count() << ','
        << tracking_healthy << '\n';
      ++frames;
      auto f = vio.getLastFrames();
      if (f && f->imu_ready_) ++fused;
      const uint64_t ros_stamp = uint64_t(int64_t(t) + ros_clock_offset);
      if (!f || !tracking_healthy) {
        if (prediction) prediction->invalidate();
        else micro_ros.submit(ros_stamp, false);
        continue;
      }
      auto T = f->get_T_W_B();
      const auto p = T.getPosition(); const auto q = T.getEigenQuaternion();
      if (!p.allFinite() || !q.coeffs().allFinite()) throw std::runtime_error("Non-finite pose");
      ++tracked;
      // Smoke modes use test geometry and must never advertise a usable pose.
      if (smoke) micro_ros.submit(ros_stamp, false);
      else if (!prediction) micro_ros.submit(ros_stamp, true, p.data(), q.coeffs().data());
      if (prediction) {
        schur_vins::SchurVINS::ImuSnapshot snapshot;
        if (f->imu_ready_ && vio.schur_vins_ && vio.schur_vins_->getImuSnapshot(snapshot)) {
          if (std::abs(snapshot.timestamp - t * 1e-9) > 1e-6)
            throw std::runtime_error("Visual correction timestamp differs from camera time");
          PredictedState corrected;
          corrected.stamp_ns=t; corrected.q=snapshot.orientation;
          corrected.p=snapshot.position; corrected.v=snapshot.velocity;
          corrected.ba=snapshot.accel_bias; corrected.bg=snapshot.gyro_bias; corrected.gravity=snapshot.gravity;
          prediction->correct(corrected);
        } else prediction->invalidate();
      }
      if (poses.is_open()) poses << t << std::setprecision(12) << ',' << p.x() << ',' << p.y() << ',' << p.z()
        << ',' << q.x() << ',' << q.y() << ',' << q.z() << ',' << q.w() << '\n';
    }
    std::cout << "frames=" << frames << " tracking=" << tracked << " imu_fused=" << fused << '\n';
    if (camera) {
      // Stop IMU before the VI teardown (which can also delay kernel scheduling).
      if (live_imu) { live_imu->stop(); live_imu->check(); }
      if (live->stop()) throw std::runtime_error("Camera helper exited with an error during cleanup");
      live->report();
      if (recording) recording->finish();
      if (live_imu) {
        live_imu->report();
        std::cout << "schur_imu_added=" << imu_added << " max_imu_batch=" << max_imu_batch << " startup_pairs_skipped=" << startup_pairs_skipped << '\n';
        if (fused != frames || !fused) throw std::runtime_error("Not every camera frame received real IMU coverage");
      }
      std::cout << "frontend_frames=" << frames << " max_input_age_ms=" << max_age_ms
                << " image_span_s=" << (last_live_stamp-first_live_stamp)*1e-9 << '\n';
      if (!frames) throw std::runtime_error("No camera images reached visual frontend");
      if (smoke) {
        std::cout << (live_vio ? "LIVE VIO INPUT PASS (real camera + IMU, test calibration; not an accuracy test)\n" :
                                "CAMERA INPUT PASS (test geometry; no IMU, no VIO accuracy claim)\n");
        return 0;
      }
    }
    if (prediction) {
      prediction->finish();
      if (prediction_csv.is_open()) { prediction_csv.flush(); if (!prediction_csv) throw std::runtime_error("Prediction CSV flush failed"); }
      std::cout << "predicted_poses=" << predicted_count << '\n';
    }
    if (tracked == 0 || (use_imu && fused == 0) || (self && (tracked != 6 || fused != 6)))
      throw std::runtime_error("Stereo/IMU pipeline did not track expected frames");
    std::cout << (self ? "SELF-TEST PASS (synthetic data; not a VIO accuracy test)\n" : "Completed\n");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << '\n'; return 1;
  }
}

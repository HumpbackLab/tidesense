// Native TideSense capture, FP64 OpenVINS backend, optional micro-ROS poses.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sys/resource.h>
#include <time.h>
#include <type_traits>
#include <utility>

#include "core/VioManager.h"
#include "core/VioManagerOptions.h"
#include "state/State.h"
#include "state/StateHelper.h"
#include "utils/opencv_yaml_parse.h"
#include "utils/print.h"
#include "track/TrackBase.h"
#include <opencv2/imgcodecs.hpp>
#include "live_camera.hpp"
#include "live_imu.hpp"
#include "imu_intrinsics.hpp"
#include "micro_ros_bridge.hpp"
#ifdef OPENVINS_C906_TPU_LK
#include "tpu_lk.hpp"
#ifdef OPENVINS_C906_FRONTEND
#include "front_tpu.hpp"
#endif
#endif

static_assert(std::is_same<decltype(ov_msckf::StateHelper::get_full_covariance(
    std::declval<std::shared_ptr<ov_msckf::State>>())), Eigen::MatrixXd>::value,
    "Live OpenVINS requires FP64 covariance");

namespace {
class LiveVioManager : public ov_msckf::VioManager {
 public:
  using ov_msckf::VioManager::VioManager;
  // Upstream initialized() additionally requires timelastupdate, which is not
  // set by startup ZUPT. The initialized filter state is valid during ZUPT too.
  bool live_initialized() const { return is_initialized_vio; }
  bool zero_velocity_update() const { return did_zupt_update; }
  std::pair<size_t, size_t> tracked_points() {
    const auto observations = trackFEATS->get_last_obs();
    const auto left = observations.find(0), right = observations.find(1);
    return {left == observations.end() ? 0 : left->second.size(),
            right == observations.end() ? 0 : right->second.size()};
  }
};
volatile sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
uint64_t clock_ns(clockid_t clock) {
  timespec value{};
  if (clock_gettime(clock, &value)) throw std::runtime_error("clock_gettime failed");
  return uint64_t(value.tv_sec) * 1000000000ULL + value.tv_nsec;
}
uint64_t shifted(uint64_t stamp, int64_t offset) {
  if (stamp > uint64_t(INT64_MAX) ||
      (offset > 0 && stamp > uint64_t(INT64_MAX - offset)) ||
      (offset < 0 && stamp <= uint64_t(-(offset + 1)) + 1))
    throw std::runtime_error("Timestamp offset out of range");
  return uint64_t(int64_t(stamp) + offset);
}
void bridge_environment() {
  for (const char* suffix : {"", "_AGENT", "_PORT", "_DOMAIN"}) {
    const auto openvins = std::string("OPENVINS_MICRO_ROS") + suffix;
    const auto shared = std::string("SCHUR_MICRO_ROS") + suffix;
    const char* value = std::getenv(openvins.c_str());
    if (value) setenv(shared.c_str(), value, 1);
    else if (!*suffix) setenv(shared.c_str(), "0", 1);
  }
}
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << "openvins_live CONFIG.yaml SENSOR.ini IMU_DEVICE POSES.csv [FRAMES]\n"
              << "FP64 backend, original EKF products; optional OPENVINS_MICRO_ROS=1\n"
              << "micro-ROS compiled: " << (MicroRosBridge::compiled() ? "yes" : "no") << '\n';
    return 0;
  }
  if (argc != 5 && argc != 6) {
    std::cerr << "Usage: openvins_live CONFIG.yaml SENSOR.ini IMU_DEVICE POSES.csv [FRAMES]\n";
    return 2;
  }
  try {
    const long limit = argc == 6 ? std::stol(argv[5]) : 0;
    if (limit < 0) throw std::runtime_error("FRAMES must be nonnegative");
    const char* mode = std::getenv("OPENVINS_EKF_MA_MODE");
    if (mode && std::string(mode) != "original")
      throw std::runtime_error("Live runner requires OPENVINS_EKF_MA_MODE=original (FP64)");
    setenv("OPENVINS_EKF_MA_MODE", "original", 1);
    bridge_environment();
    const std::string config_path=argv[1];
    const auto slash=config_path.find_last_of('/');
    const std::string directory=slash==std::string::npos ? "." : config_path.substr(0,slash);
    const auto intrinsics=ImuIntrinsics::parse(YAML::LoadFile(directory+"/imu_intrinsics.yaml")["imu_params"]);
    if (intrinsics.input_is_corrected) throw std::runtime_error("Live IIO requires nominal raw SI IMU input");
    auto parser = std::make_shared<ov_core::YamlParser>(argv[1]);
    ov_core::Printer::setPrintLevel("WARNING");
    ov_msckf::VioManagerOptions options;
    options.print_and_load(parser);
    options.num_opencv_threads = 1;
    options.use_multi_threading_pubs = false;
    options.use_multi_threading_subs = false;
    if (!parser->successful() || options.state_options.num_cameras != 2 || !options.use_stereo ||
        options.state_options.do_calib_camera_timeoffset)
      throw std::runtime_error("Expected valid stereo config with fixed calibrated time offset");
    if (!std::isfinite(options.calib_camimu_dt) || std::abs(options.calib_camimu_dt) > 1)
      throw std::runtime_error("Camera offset exceeds one second or is not finite");
    const int64_t camera_offset = std::llround(options.calib_camimu_dt * 1e9);
    LiveVioManager system(options);
    std::ofstream poses(argv[4]);
    if (!poses) throw std::runtime_error("Cannot open pose CSV");
    poses << "camera_timestamp_ns,imu_timestamp_ns,sequence,initialized,updated,px,py,pz,qx,qy,qz,qw,camera_ms,frame_ms,frame_age_ms,msckf_points,slam_points";
#ifdef OPENVINS_C906_TPU_LK
    poses << ",tpu_hits,tpu_prepare_ms,tpu_remote_ms,tpu_wait_cpu_ms,tpu_overlap_ms" << ov_core::TpuLk::diagnostic_header();
#ifdef OPENVINS_C906_FRONTEND
    poses << ov_core::TpuFront::diagnostic_header();
#endif
#endif
    poses << '\n';
    poses << std::setprecision(15);
    struct sigaction action{};
    action.sa_handler = interrupt;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    const char* helper = std::getenv("OPENVINS_CAMERA_HELPER");
    LiveCamera camera(helper ? helper : executableDirectory() + "/schurvins_camera", argv[2]);
    LivePair startup;
    if (!camera.next(startup, interrupted)) return 0;
    LiveImu imu(argv[3]);
    const uint64_t ready_ns = clock_ns(CLOCK_MONOTONIC) + 100000000ULL;
    // Fork the camera helper before starting the micro-ROS communication thread.
    MicroRosBridge bridge;
    const int64_t ros_offset = int64_t(clock_ns(CLOCK_REALTIME)) - int64_t(clock_ns(CLOCK_MONOTONIC));
    std::cerr << "openvins: precision=fp64 original, FP64 products/covariance/solve/writeback, num_pts=" << options.num_pts
              << " per_camera_target=" << options.num_pts / 2 << " right_rotation=180 camera_offset_ns=" << camera_offset
              << " micro_ros=" << bridge.enabled() << " ZUPT_startup=" << options.try_zupt << '\n';
#ifdef OPENVINS_C906_TPU_LK
    std::cerr << "openvins: tpu_lk=" << ov_core::TpuLk::enabled()
              << " lookahead_pipeline=" << ov_core::TpuLk::pipeline()
#ifdef OPENVINS_C906_FRONTEND
              << " frontend=" << ov_core::TpuFront::enabled()
              << " front_early=" << ov_core::TpuFront::early()
              << " latest_intrinsics=" << ov_core::TpuFront::latest_intrinsics()
#endif
              << '\n';
#endif
    uint64_t frames = 0, initialized = 0, imu_count = 0, last_imu = 0, last_camera = 0, first_camera = 0;
    uint64_t sequence_gaps = 0, previous_sequence = 0, startup_skipped = 1;
    double camera_sum = 0, initialized_sum = 0, camera_max = 0, age_max = 0;
    bool was_initialized = false;
    LivePair lookahead_pair;
    bool have_lookahead_pair = false;
    const auto begin = std::chrono::steady_clock::now();
    while (!interrupted && (!limit || frames < uint64_t(limit))) {
      LivePair pair;
      if (have_lookahead_pair) {
        pair = std::move(lookahead_pair);
        have_lookahead_pair = false;
      } else if (!camera.next(pair, interrupted)) break;
      if (!(pair.header.flags & CAMERA_FSIN)) throw std::runtime_error("Hardware-synchronized stereo required");
      const auto raw_ns = pair.header.left_ns;
      const uint64_t imu_ns = shifted(raw_ns, camera_offset);
      if (imu_ns < ready_ns) { ++startup_skipped; continue; }
      if (raw_ns <= last_camera) throw std::runtime_error("Camera timestamp is not increasing");
      const uint64_t now = clock_ns(CLOCK_MONOTONIC);
      if (now < raw_ns) throw std::runtime_error("Camera PTS must use CLOCK_MONOTONIC");
      double age_ms = (now - raw_ns) * 1e-6;
      age_max = std::max(age_max, age_ms);
      if (!first_camera) first_camera = raw_ns;
      last_camera = raw_ns;
      if (previous_sequence) sequence_gaps += pair.header.sequence - previous_sequence - 1;
      previous_sequence = pair.header.sequence;
      const auto frame_begin = std::chrono::steady_clock::now();
      auto samples = imu.through(imu_ns, interrupted);
      if (interrupted) break;
      for (const auto& sample : samples) {
        if (sample.stamp_ns <= last_imu || (!last_imu && sample.stamp_ns >= imu_ns))
          throw std::runtime_error("IMU must increase and start before the first aligned camera frame");
        ov_core::ImuData data;
        data.timestamp = sample.stamp_ns * 1e-9;
        const auto corrected=intrinsics.apply(sample);
        data.wm = Eigen::Vector3d(corrected.gyro[0], corrected.gyro[1], corrected.gyro[2]);
        data.am = Eigen::Vector3d(corrected.accel[0], corrected.accel[1], corrected.accel[2]);
        system.feed_measurement_imu(data);
        last_imu = sample.stamp_ns;
        ++imu_count;
      }
      if (last_imu <= imu_ns) throw std::runtime_error("IMU must bracket the aligned camera time");
      ov_core::CameraData image;
      image.timestamp = raw_ns * 1e-9; // OpenVINS adds the calibrated offset internally.
      image.sensor_ids = {0, 1};
      cv::flip(pair.right, pair.right, -1);
      image.images = {pair.left, pair.right};
      image.masks = {cv::Mat::zeros(pair.left.size(), CV_8UC1), cv::Mat::zeros(pair.right.size(), CV_8UC1)};
#ifdef OPENVINS_C906_TPU_LK
      // Before initialization there is no backend to overlap; consume latest frames.
      if (ov_core::TpuLk::pipeline() && system.live_initialized() && (!limit || frames + 1 < uint64_t(limit))) {
        if (!camera.next(lookahead_pair, interrupted)) break;
        have_lookahead_pair = true;
        if (!(lookahead_pair.header.flags & CAMERA_FSIN) || lookahead_pair.header.left_ns <= raw_ns)
          throw std::runtime_error("Invalid synchronized lookahead camera frame");
        ov_core::CameraData hint;
        hint.timestamp = lookahead_pair.header.left_ns * 1e-9;
        hint.sensor_ids = {0, 1};
        cv::Mat right;
        cv::flip(lookahead_pair.right, right, -1);
        hint.images = {lookahead_pair.left, right};
        hint.masks = {cv::Mat::zeros(right.size(), CV_8UC1), cv::Mat::zeros(right.size(), CV_8UC1)};
        system.set_next_camera_for_lk(hint);
        age_ms = (clock_ns(CLOCK_MONOTONIC) - raw_ns) * 1e-6;
        age_max = std::max(age_max, age_ms);
      }
#endif
#ifdef OPENVINS_C906_TPU_LK
      const auto tpu_before = ov_core::TpuLk::stats();
#ifdef OPENVINS_C906_FRONTEND
      const auto front_before = ov_core::TpuFront::stats();
#endif
#endif
      const auto camera_begin = std::chrono::steady_clock::now();
#ifdef OPENVINS_C906_TPU_LK
      const double camera_trace_begin=ov_core::TpuLk::now_ms(),camera_trace_cpu=ov_core::TpuLk::thread_cpu_ms();
#endif
      system.feed_measurement_camera(image);
#ifdef OPENVINS_C906_TPU_LK
      ov_core::TpuLk::record("camera",image.timestamp,camera_trace_begin,ov_core::TpuLk::now_ms(),
          ov_core::TpuLk::thread_cpu_ms()-camera_trace_cpu);
#endif
      const auto camera_end = std::chrono::steady_clock::now();
      if (frames == 99) {
        if (const char* snapshots = std::getenv("OPENVINS_SNAPSHOT_DIR")) {
          const auto directory = std::string(snapshots) + "/";
          if (!cv::imwrite(directory + "left.png", pair.left) ||
              !cv::imwrite(directory + "right-rotated.png", pair.right) ||
              !cv::imwrite(directory + "tracks.png", system.get_historical_viz_image()))
            throw std::runtime_error("Cannot write diagnostic snapshots");
        }
      }
      const double camera_ms = std::chrono::duration<double, std::milli>(camera_end - camera_begin).count();
      camera_sum += camera_ms;
      camera_max = std::max(camera_max, camera_ms);
      const bool is_initialized = system.live_initialized();
      const auto state = system.get_state();
      const bool updated = is_initialized && std::abs(state->_timestamp - image.timestamp) < 1e-6;
      Eigen::Vector3d position = Eigen::Vector3d::Zero();
      Eigen::Vector4d orientation(0, 0, 0, 1);
      if (is_initialized) {
        position = state->_imu->pos();
        // OpenVINS uses JPL q_GtoI; ROS uses Hamilton q_ItoG. Convert the
        // rotation matrix explicitly to avoid accidentally conjugating twice.
        orientation = Eigen::Quaterniond(state->_imu->Rot().transpose()).normalized().coeffs();
        if (!position.allFinite() || !orientation.allFinite() || std::abs(orientation.norm() - 1) > 1e-3)
          throw std::runtime_error("Invalid OpenVINS pose");
        const Eigen::MatrixXd covariance = ov_msckf::StateHelper::get_full_covariance(state);
        if (!covariance.allFinite() || covariance.diagonal().minCoeff() < 0)
          throw std::runtime_error("Nonfinite or negative covariance diagonal");
        ++initialized;
        initialized_sum += camera_ms;
      }
      const uint64_t ros_ns = shifted(imu_ns, ros_offset);
      bridge.submit(ros_ns, updated, updated ? position.data() : nullptr, updated ? orientation.data() : nullptr);
      const size_t msckf_points = system.get_good_features_MSCKF().size();
      const size_t slam_points = state->_features_SLAM.size();
      const double frame_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_begin).count();
      poses << raw_ns << ',' << imu_ns << ',' << pair.header.sequence << ',' << is_initialized << ',' << updated;
      if (is_initialized) {
        poses << ',' << position[0] << ',' << position[1] << ',' << position[2]
              << ',' << orientation[0] << ',' << orientation[1] << ',' << orientation[2] << ',' << orientation[3];
      } else poses << ",,,,,,,";
      poses << ',' << camera_ms << ',' << frame_ms << ',' << age_ms << ',' << msckf_points << ',' << slam_points;
#ifdef OPENVINS_C906_TPU_LK
      const auto tpu_after = ov_core::TpuLk::stats();
      poses << ',' << tpu_after.cache_hits - tpu_before.cache_hits
            << ',' << tpu_after.prepare_ms - tpu_before.prepare_ms
            << ',' << tpu_after.remote_ms - tpu_before.remote_ms
            << ',' << tpu_after.wait_cpu_ms - tpu_before.wait_cpu_ms
            << ',' << tpu_after.overlap_ms - tpu_before.overlap_ms
            << ov_core::TpuLk::diagnostic_delta(tpu_before);
#ifdef OPENVINS_C906_FRONTEND
      poses << ov_core::TpuFront::diagnostic_delta(front_before);
#endif
#endif
      poses << '\n';
      if (!poses) throw std::runtime_error("Pose CSV write failed");
      ++frames;
      if (is_initialized && !was_initialized) std::cerr << "openvins: INITIALIZED frame=" << frames << '\n';
      was_initialized = is_initialized;
      if (frames % 20 == 0) {
        const auto tracked = system.tracked_points();
        imu.check();
        poses.flush();
        std::cerr << "openvins: frames=" << frames << " initialized=" << initialized << " updated=" << updated
                  << " avg_camera_ms=" << camera_sum / frames << " camera_ms=" << camera_ms
                  << " imu=" << imu_count << " sequence_gaps=" << sequence_gaps
                  << " msckf=" << msckf_points << " slam=" << slam_points
                  << " tracks=" << tracked.first << '/' << tracked.second
                  << " zupt=" << system.zero_velocity_update()
                  << " p=" << position.transpose() << '\n';
      }
    }
    poses.flush();
    const int camera_status = camera.stop();
    imu.stop();
    camera.report();
    imu.report();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cerr << "openvins: DONE frames=" << frames << " initialized=" << initialized
              << " avg_camera_ms=" << (frames ? camera_sum / frames : 0)
              << " avg_initialized_ms=" << (initialized ? initialized_sum / initialized : 0)
              << " max_camera_ms=" << camera_max << " max_frame_age_ms=" << age_max
              << " consumed_fps=" << (last_camera > first_camera ? (frames - 1) * 1e9 / (last_camera - first_camera) : 0)
              << " sequence_gaps=" << sequence_gaps << " startup_skipped=" << startup_skipped
              << " peak_rss_kb=" << usage.ru_maxrss
              << " wall_s=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() << '\n';
#ifdef OPENVINS_C906_TPU_LK
    const auto tpu_total = ov_core::TpuLk::stats();
    std::cerr << "openvins: TPU calls=" << tpu_total.calls << " points=" << tpu_total.points
              << " hits=" << tpu_total.cache_hits << " overlap_ms=" << tpu_total.overlap_ms
              << " wait_cpu_ms=" << tpu_total.wait_cpu_ms << '\n';
#ifdef OPENVINS_C906_FRONTEND
    const auto front_total = ov_core::TpuFront::stats();
    std::cerr << "openvins: FRONT calls=" << front_total.calls
              << " avg_device_ms=" << (front_total.calls ? front_total.total_ms / front_total.calls : 0)
              << " snapshot_wait_ms=" << front_total.snapshot_wait_ms
              << " upload_cpu_ms=" << front_total.prepare_cpu_ms << '\n';
#endif
#endif
    return camera_status || !frames ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << "openvins: ERROR: " << error.what() << '\n';
    return 1;
  }
}

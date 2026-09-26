#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include "core/VioManager.h"
#include "core/VioManagerOptions.h"
#include "state/State.h"
#include "utils/opencv_yaml_parse.h"
#include "utils/print.h"
#include "utils/sensor_data.h"

static std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) result.push_back(field);
  return result;
}

static cv::Mat read_image(const std::string &path) {
  cv::Mat image = cv::imread(path, cv::IMREAD_GRAYSCALE);
  if (image.empty()) throw std::runtime_error("cannot read image: " + path);
  return image;
}

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "usage: run_record config.yaml record_dir trajectory.csv frame_limit\n";
    return 2;
  }
  try {
    const std::string record = argv[2];
    const int frame_limit = std::stoi(argv[4]);
    auto parser = std::make_shared<ov_core::YamlParser>(argv[1]);
    ov_core::Printer::setPrintLevel("WARNING");
    ov_msckf::VioManagerOptions options;
    options.print_and_load(parser);
    options.num_opencv_threads = 1;
    options.use_multi_threading_pubs = false;
    options.use_multi_threading_subs = false;
    if (!parser->successful()) throw std::runtime_error("config parse failed");
    ov_msckf::VioManager system(options);

    std::ifstream imu_file(record + "/imu.csv");
    std::vector<ov_core::ImuData> imu;
    std::string line;
    while (std::getline(imu_file, line)) {
      if (line.empty() || line[0] == '#') continue;
      auto f = fields(line);
      if (f.size() < 7) throw std::runtime_error("bad IMU CSV");
      ov_core::ImuData m;
      m.timestamp = std::stod(f[0]) * 1e-9;
      m.wm << std::stod(f[1]), std::stod(f[2]), std::stod(f[3]);
      m.am << std::stod(f[4]), std::stod(f[5]), std::stod(f[6]);
      imu.push_back(m);
    }
    if (imu.empty()) throw std::runtime_error("empty IMU CSV");
    std::ifstream frames(record + "/stereo.csv");
    std::ofstream poses(argv[3]);
    poses << "timestamp_ns,initialized,px,py,pz,qx,qy,qz,qw,compute_ms\n";
    size_t imu_cursor = 0;
    int frame_count = 0;
    int initialized_count = 0;
    double total_ms = 0;
    const auto wall_start = std::chrono::steady_clock::now();
    while (std::getline(frames, line) && (frame_limit <= 0 || frame_count < frame_limit)) {
      if (line.empty() || line[0] == '#') continue;
      auto f = fields(line);
      if (f.size() < 3) throw std::runtime_error("bad stereo CSV");
      uint64_t time_ns = std::stoull(f[0]);
      const double camera_time = time_ns * 1e-9;
      while (imu_cursor < imu.size() && imu[imu_cursor].timestamp <= camera_time + 0.03) {
        system.feed_measurement_imu(imu[imu_cursor++]);
      }
      ov_core::CameraData image;
      image.timestamp = camera_time;
      image.sensor_ids = {0, 1};
      image.images = {read_image(record + "/" + f[1]), read_image(record + "/" + f[2])};
      image.masks = {cv::Mat::zeros(image.images[0].size(), CV_8UC1), cv::Mat::zeros(image.images[1].size(), CV_8UC1)};
      const auto start = std::chrono::steady_clock::now();
      system.feed_measurement_camera(image);
      const auto end = std::chrono::steady_clock::now();
      double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
      total_ms += elapsed_ms;
      bool initialized = system.initialized();
      initialized_count += initialized;
      poses << time_ns << ',' << initialized;
      if (initialized) {
        auto state = system.get_state();
        auto p = state->_imu->pos();
        auto q = state->_imu->quat();
        poses << std::setprecision(15) << ',' << p[0] << ',' << p[1] << ',' << p[2]
              << ',' << q[0] << ',' << q[1] << ',' << q[2] << ',' << q[3];
      } else poses << ",,,,,,,";
      poses << ',' << elapsed_ms << '\n';
      if (++frame_count % 50 == 0) std::cerr << "frames=" << frame_count << " initialized=" << initialized_count
                                             << " imu=" << imu_cursor << " avg_ms=" << total_ms / frame_count << '\n';
    }
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    std::cerr << "DONE frames=" << frame_count << " initialized=" << initialized_count
              << " imu=" << imu_cursor << " avg_ms=" << total_ms / frame_count
              << " wall_s=" << wall_s << " peak_rss_kb=" << usage.ru_maxrss << '\n';
    return frame_count > 0 ? 0 : 1;
  } catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << '\n';
    return 1;
  }
}

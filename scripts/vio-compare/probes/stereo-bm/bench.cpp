#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}
static void read_exact(std::ifstream &f, void *p, size_t n) {
  f.read(static_cast<char *>(p), n);
  if (!f) throw std::runtime_error("Incomplete maps file");
}
int main(int argc, char **argv) try {
  if (argc != 7) {
    std::cerr << "Usage: stereo_bm DATASET MAPS NUM_DISPARITIES BLOCK_SIZE TIMING_CSV SAMPLE_PREFIX\n";
    return 2;
  }
  cv::setNumThreads(1);
  cv::setUseOptimized(true);
  std::ifstream f(argv[2], std::ios::binary);
  uint32_t w, h;
  double fx, baseline;
  read_exact(f, &w, 4); read_exact(f, &h, 4);
  read_exact(f, &fx, 8); read_exact(f, &baseline, 8);
  if (w > 640 || h > 400 || !w || !h) throw std::runtime_error("Invalid map dimensions");
  cv::Mat m1[2], m2[2], mask(h, w, CV_8UC1);
  for (int i = 0; i < 2; ++i) {
    m1[i].create(h, w, CV_16SC2); m2[i].create(h, w, CV_16UC1);
    read_exact(f, m1[i].data, w * h * 4); read_exact(f, m2[i].data, w * h * 2);
  }
  read_exact(f, mask.data, w * h);
  const int nd = std::stoi(argv[3]), block = std::stoi(argv[4]);
  auto bm = cv::StereoBM::create(nd, block);
  bm->setMinDisparity(0);
  bm->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);
  bm->setPreFilterCap(31);
  bm->setTextureThreshold(10);
  bm->setUniquenessRatio(15);
  bm->setSpeckleWindowSize(100);
  bm->setSpeckleRange(2);
  bm->setDisp12MaxDiff(1);
  std::ifstream csv(std::string(argv[1]) + "/stereo.csv");
  if (!csv) throw std::runtime_error("Cannot open stereo.csv");
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(csv, line)) if (!line.empty() && line[0] != '#') lines.push_back(line);
  std::ofstream timings(argv[5]);
  if (!timings) throw std::runtime_error("Cannot open timing output");
  timings << "frame,timestamp_ns,read_ms,rectify_ms,bm_ms,depth_ms,process_ms,with_read_ms,valid_fraction\n";
  // Optional perf time windows; all serialization happens outside frame timers.
  std::ofstream intervals;
  if (const char *path = std::getenv("STEREO_BM_INTERVALS")) {
    intervals.open(path);
    if (!intervals) throw std::runtime_error("Cannot open perf interval output");
    intervals << "frame,read_begin_ns,rectify_begin_ns,bm_begin_ns,depth_begin_ns,end_ns\n";
  }
  cv::Mat rect[2], disparity, depth(h, w, CV_16UC1);
  std::cout << "OpenCV " << CV_VERSION << " threads=" << cv::getNumThreads()
            << " optimized=" << cv::useOptimized() << " size=" << w << "x" << h
            << " disparities=" << nd << " block=" << block << " fx=" << fx
            << " baseline=" << baseline << " frames=" << lines.size() << std::endl;
  for (size_t n = 0; n < lines.size(); ++n) {
    const auto &s = lines[n];
    auto c1 = s.find(','), c2 = s.find(',', c1 + 1);
    if (c1 == std::string::npos || c2 == std::string::npos) throw std::runtime_error("Bad CSV row");
    auto t0 = Clock::now();
    cv::Mat raw[2] = {
      cv::imread(std::string(argv[1]) + "/" + s.substr(c1 + 1, c2 - c1 - 1), 0),
      cv::imread(std::string(argv[1]) + "/" + s.substr(c2 + 1), 0)};
    if (raw[0].size() != cv::Size(640,400) || raw[1].size() != cv::Size(640,400))
      throw std::runtime_error("Bad source image");
    auto t1 = Clock::now();
    // Map order is physical right then physical left: positive disparity.
    cv::remap(raw[1], rect[0], m1[0], m2[0], cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    cv::remap(raw[0], rect[1], m1[1], m2[1], cv::INTER_LINEAR, cv::BORDER_CONSTANT);
    auto t2 = Clock::now();
    bm->compute(rect[0], rect[1], disparity);
    auto t3 = Clock::now();
    size_t count = 0;
    const float scale = static_cast<float>(fx * baseline * 16000.);
    for (uint32_t y = 0; y < h; ++y) {
      auto d = disparity.ptr<int16_t>(y);
      auto z = depth.ptr<uint16_t>(y);
      auto good = mask.ptr<uint8_t>(y);
      for (uint32_t x = 0; x < w; ++x) {
        z[x] = 0;
        if (d[x] > 0 && good[x]) {
          float v = scale / d[x];
          if (v >= 1 && v <= 65535) { z[x] = static_cast<uint16_t>(v + .5f); ++count; }
        }
      }
    }
    auto t4 = Clock::now();
    if (intervals.is_open()) {
      intervals << n;
      for (auto time : {t0, t1, t2, t3, t4})
        intervals << ',' << std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
      intervals << '\n';
    }
    timings << n << ',' << s.substr(0,c1) << ',' << ms(t0,t1) << ',' << ms(t1,t2)
            << ',' << ms(t2,t3) << ',' << ms(t3,t4) << ',' << ms(t1,t4)
            << ',' << ms(t0,t4) << ',' << double(count)/(w*h) << '\n';
    if (n == 150 || n == 400 || n == 600) {
      const std::string prefix = std::string(argv[6]) + "-" + std::to_string(n);
      cv::imwrite(prefix + "-rect0.pgm", rect[0]);
      cv::imwrite(prefix + "-rect1.pgm", rect[1]);
      cv::imwrite(prefix + "-depth.png", depth);
      std::ofstream disp(prefix + "-disparity.bin", std::ios::binary);
      disp.write(reinterpret_cast<const char *>(disparity.data), w*h*2);
    }
  }
  std::cout << "DONE " << lines.size() << " pairs\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n'; return 1;
}

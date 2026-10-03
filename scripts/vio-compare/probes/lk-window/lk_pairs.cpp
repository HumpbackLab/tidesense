// Fixed-input LK comparison: all windows receive the same image pairs/points.
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  cv::setNumThreads(1);
  std::string root = argv[1], line;
  std::ifstream input(root + "/stereo.csv");
  std::vector<std::array<std::string, 3>> frames;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::stringstream ss(line);
    std::array<std::string, 3> row;
    for (auto &field : row) std::getline(ss, field, ',');
    frames.push_back(row);
  }
  if (frames.size() < 203) return 2;
  const std::array<cv::Size, 3> windows = {cv::Size(15,15), cv::Size(16,15), cv::Size(16,16)};
  std::cout << "frame,camera,window,trial,points,valid,compute_ms\n";
  for (size_t pair = 0; pair < 32; ++pair) {
    const size_t index = 200 + pair * (frames.size() - 202) / 31;
    for (int camera = 1; camera <= 2; ++camera) {
      cv::Mat a = cv::imread(root + "/" + frames[index][camera], cv::IMREAD_GRAYSCALE);
      cv::Mat b = cv::imread(root + "/" + frames[index+1][camera], cv::IMREAD_GRAYSCALE);
      if (a.empty() || b.empty()) return 1;
      cv::equalizeHist(a, a);
      cv::equalizeHist(b, b);
      std::vector<cv::KeyPoint> keypoints;
      cv::FAST(a, keypoints, 50, true);
      std::vector<cv::Point2f> points;
      const size_t count = std::min(size_t(200), keypoints.size());
      for (size_t k = 0; k < count; ++k)
        points.push_back(keypoints[k * keypoints.size() / count].pt);
      if (points.size() < 10) continue;
      std::array<std::vector<cv::Mat>, 3> pa, pb;
      for (int w = 0; w < 3; ++w) {
        cv::buildOpticalFlowPyramid(a, pa[w], windows[w], 5);
        cv::buildOpticalFlowPyramid(b, pb[w], windows[w], 5);
      }
      for (int trial = 0; trial < 4; ++trial) {
        for (int order = 0; order < 3; ++order) {
          const int w = (order + trial + pair) % 3;
          auto next = points;
          std::vector<unsigned char> status;
          std::vector<float> error;
          const auto start = std::chrono::steady_clock::now();
          cv::calcOpticalFlowPyrLK(pa[w], pb[w], points, next, status, error, windows[w], 5,
              cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01),
              cv::OPTFLOW_USE_INITIAL_FLOW);
          const double ms = std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start).count();
          size_t valid = 0;
          for (auto value : status) valid += value != 0;
          if (trial > 0) std::cout << index << ',' << camera-1 << ','
              << windows[w].width << 'x' << windows[w].height << ',' << trial << ','
              << points.size() << ',' << valid << ',' << ms << '\n';
        }
      }
    }
  }
}

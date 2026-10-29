#pragma once
#include <atomic>
#include <future>
#include <memory>
#include <vector>
#include <string>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>

namespace ov_core {
struct TpuFrontRequest;
// Raw optical flow only: no camera calibration, feature IDs, or database access.
class TpuLk {
public:
  struct Stats {
    uint64_t calls=0, points=0, cache_hits=0, prefetched=0;
    double prepare_ms=0, remote_ms=0, wait_cpu_ms=0, overlap_ms=0;
    double kickoff_wait_ms=0, consume_wait_ms=0, backend_wall_ms=0, backend_cpu_ms=0;
    double worker_wall_ms=0, worker_cpu_ms=0, preprocess_ms=0, detection_ms=0;
    double c906_ms=0, tpu_device_ms=0, c906_backend_overlap_ms=0, tpu_backend_overlap_ms=0;
    double consume_c906_ms=0, consume_tpu_ms=0;
  };
  struct Event { const char *kind; double begin,end,cpu=0; };
  struct Request {
    std::vector<cv::Mat> previous, next;
    std::vector<cv::Point2f> input, initial, output;
    std::vector<unsigned char> status;
    std::shared_ptr<void> workspace;
    std::atomic<bool> started{false};
    bool cacheable=true;
    std::vector<std::pair<double,double>> spans, c906_spans, tpu_spans;
    double clock_uncertainty_ms=0;
    double prepare_ms=0, remote_ms=0, wait_cpu_ms=0;
  };
  // Worker requests are accounted on the main thread after future::get().
  class Deferred {
  public:
    explicit Deferred(std::vector<std::shared_ptr<Request>>& requests, std::promise<void>* submitted=nullptr);
    ~Deferred();
    Deferred(const Deferred&)=delete;
    Deferred& operator=(const Deferred&)=delete;
  };
  static bool enabled();
  static bool pipeline();
  static double now_ms();
  static double thread_cpu_ms();
  static bool kickoff_wait();
  static void record(const char *kind,double stamp,double begin,double end,double cpu=0,double uncertainty=0);
  static std::string diagnostic_header();
  static std::string diagnostic_delta(const Stats& previous);
  static std::shared_ptr<Request> prepare(const std::vector<cv::Mat>& previous,
      const std::vector<cv::Mat>& next, const std::vector<cv::Point2f>& input,
      const std::vector<cv::Point2f>& initial, std::shared_ptr<void> reuse={});
  static void execute(const std::shared_ptr<Request>&);
  static void accept(const std::shared_ptr<Request>&, double backend_begin, double backend_end,
      double stamp=0,double consume_begin=0,double consume_end=0);
  static void flow(const std::vector<cv::Mat>&, const std::vector<cv::Mat>&,
      const std::vector<cv::Point2f>&, std::vector<cv::Point2f>&,
      std::vector<unsigned char>&, std::vector<float>&, cv::Size, int, cv::TermCriteria, int);
  static Stats stats();
  static void clear_cache();
};

struct TpuNextFrame {
  double timestamp=0, backend_begin=0, backend_end=0;
  std::vector<int> ids;
  std::vector<cv::Mat> images;
  std::vector<std::vector<cv::Mat>> pyramids;
  std::vector<std::shared_ptr<TpuLk::Request>> requests;
  std::vector<TpuLk::Event> events;
  std::vector<std::vector<cv::KeyPoint>> old_points;
  std::vector<std::vector<size_t>> old_ids;
  size_t currid_end=0;
  bool detection_ready=false;
  std::atomic<bool> worker_started{false};
  // Last: joining the future precedes destruction of captured frame buffers.
  std::shared_ptr<TpuFrontRequest> frontend;
  std::shared_future<std::shared_ptr<TpuFrontRequest>> prepared_front;
  std::future<void> worker;
};
}

#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <opencv2/features2d.hpp>
#include "utils/sensor_data.h"
#include "../probes/c906-frontend/front_shared.h"
namespace ov_core {
class CamBase;
struct TpuFrontRequest {
  std::shared_ptr<void> workspace;
  front_control result{};
  double prepare_begin=0,prepare_end=0,prepare_cpu=0;
  double begin=0,end=0,wait_cpu=0;
  double snapshot_begin=0,snapshot_end=0,snapshot_cpu=0;
  unsigned tail_jobs=0;
  double tail_ms=0,tail_cpu_ms=0,tail_sync_ms=0,tail_remote_wait_ms=0;
  unsigned ive_histogram_calls=0,ive_st_calls=0;
  double ive_histogram_ms=0,ive_st_ms=0;
};
class TpuFront {
public:
  struct Stats {
    uint64_t calls=0;double prepare_ms=0,prepare_cpu_ms=0,remote_ms=0,wait_cpu_ms=0;
    double equalize_ms=0,pyramid_ms=0,detect_ms=0,lk_ms=0,total_ms=0;
    double snapshot_cpu_ms=0,snapshot_wait_ms=0;
    uint64_t tail_jobs=0;
    double tail_ms=0,tail_cpu_ms=0,tail_sync_ms=0,tail_remote_wait_ms=0;
    uint64_t ive_histogram_calls=0,ive_st_calls=0;
    double ive_histogram_ms=0,ive_st_ms=0;
  };
  static bool enabled();
  static bool early();
  static bool latest_intrinsics();
  static bool cpu_tail();
  static void assist_tail(const std::shared_ptr<TpuFrontRequest>&,double timestamp);
  static void update_intrinsics(const std::array<std::shared_ptr<CamBase>,2>&);
  static std::shared_ptr<TpuFrontRequest> prepare(const CameraData&,double previous,
      const std::array<std::vector<cv::KeyPoint>,2>&,const std::array<std::vector<size_t>,2>&,
      size_t currid,int features,int threshold,int gx,int gy,int min_px,int histogram,bool deferred=false);
  static void publish(const std::shared_ptr<TpuFrontRequest>&,double timestamp,
      const std::array<std::vector<cv::KeyPoint>,2>&,const std::array<std::vector<size_t>,2>&,
      size_t currid,int features);
  static void execute(const std::shared_ptr<TpuFrontRequest>&);
  static void accept(const std::shared_ptr<TpuFrontRequest>&,double stamp);
  static Stats stats();
  static std::string diagnostic_header();
  static std::string diagnostic_delta(const Stats&);
};
}

#pragma once
// Opt-in experiment. Vectors require either LK refinement or explicit patch verification.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <opencv2/core/core.hpp>

namespace ov_codec_mv {
struct Vector { float sx, sy, dx, dy, w, h; };
static_assert(sizeof(Vector) == 24, "Unexpected vector record layout");

struct State {
  std::string mode;
  std::string lk_mode;
  struct LKStats {
    size_t shallow = 0, full = 0, retries = 0;
    double shallow_ms = 0, full_ms = 0;
    size_t verified = 0, verify_candidates = 0, refreshed = 0;
    double verify_ms = 0;
    // Original point order. Path: 0=full LK, 1=accepted shallow, 2=retry, 3=verified MV.
    // Failure: 1=LK status, 2=nonfinite, 3=patch residual, 4=refinement, 5=bounds.
    std::vector<uchar> path, failure, valid;
  } lk_stats;
  std::ifstream input;
  FILE *detail = nullptr;
  FILE *tracks = nullptr;
  bool retry = true;
  uint64_t current_ns = 0, reference_ns = 0, previous_ns = 0;
  uint32_t frame_index = 0;
  int width = 0, height = 0, columns = 0, rows = 0;
  std::vector<Vector> vectors;
  std::vector<std::vector<uint32_t>> bins;
  std::vector<uint32_t> seen;
  uint32_t epoch = 0;

  State() {
    const char *value = std::getenv("OV_CODEC_MV_MODE");
    mode = value ? value : "off";
    value = std::getenv("OV_CODEC_MV_LK");
    lk_mode = value ? value : "full";
    if (lk_mode != "full" && lk_mode != "shallow" && lk_mode != "verify")
      throw std::runtime_error("OV_CODEC_MV_LK must be full, shallow or verify");
    value = std::getenv("OV_CODEC_MV_RETRY");
    if (value && std::string(value) != "on" && std::string(value) != "off")
      throw std::runtime_error("OV_CODEC_MV_RETRY must be on or off");
    retry = !value || std::string(value) == "on";
    if (mode != "off" && mode != "rect" && mode != "median")
      throw std::runtime_error("OV_CODEC_MV_MODE must be off, rect or median");
    if (mode != "off") {
      value = std::getenv("OV_CODEC_MV_BIN");
      if (!value) throw std::runtime_error("OV_CODEC_MV_BIN is required");
      input.open(value, std::ios::binary);
      char magic[8]{};
      input.read(magic, 8);
      if (!input || std::string(magic, 8) != "CVMV0001")
        throw std::runtime_error("Invalid codec MV input");
    }
    value = std::getenv("OV_CODEC_MV_DETAIL");
    if (value) {
      detail = std::fopen(value, "w");
      if (!detail) throw std::runtime_error("Cannot open codec MV detail CSV");
      std::fprintf(detail, "timestamp_ns,points,seeded,lk_valid,geometry_valid,seeded_geometry_valid,seed_ms,lk_ms,normalize_ms,ransac_ms,seed_error_median_px,zero_error_median_px,seed_error_p95_px,zero_error_p95_px,shallow_points,full_points,full_retries,shallow_ms,full_ms,verify_candidates,verified_points,verified_geometry_valid,refresh_points,verify_ms\n");
    }
    value = std::getenv("OV_CODEC_MV_TRACKS");
    if (value) {
      tracks = std::fopen(value, "w");
      if (!tracks) throw std::runtime_error("Cannot open codec MV track CSV");
      std::fprintf(tracks, "timestamp_ns,frame_index,feature_id,path,shallow_failure,lk_valid,geometry_valid,retained\n");
    }
    std::fprintf(stderr, "CODEC_MV mode=%s, previous-frame source footprints, FP64 backend\n", mode.c_str());
  }
  ~State() { if (detail) std::fclose(detail); if (tracks) std::fclose(tracks); }

  template<class T> void read(T &value) {
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Codec MV cache currently requires a little-endian host
#endif
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
    if (!input) throw std::runtime_error("Truncated codec MV cache");
  }

  void advance(uint64_t stamp, int w, int h) {
    if (mode == "off") { current_ns = stamp; return; }
    uint64_t recorded_stamp = 0, recorded_reference = 0;
    uint32_t index = 0, count = 0;
    read(recorded_stamp); read(recorded_reference); read(index); read(count);
    if (recorded_stamp != stamp || index != frame_index++ || recorded_reference != previous_ns)
      throw std::runtime_error("Codec MV frame/reference timestamp mismatch");
    if (count > 640 * 480) throw std::runtime_error("Unreasonable MV count");
    reference_ns = recorded_reference; current_ns = stamp; previous_ns = stamp;
    width = w; height = h; columns = (w + 15) / 16; rows = (h + 15) / 16;
    vectors.resize(count);
    input.read(reinterpret_cast<char *>(vectors.data()), count * sizeof(Vector));
    if (!input) throw std::runtime_error("Truncated vector records");
    bins.clear(); bins.resize(columns * rows);
    seen.assign(count, 0); epoch = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const auto &v = vectors[i];
      if (!std::isfinite(v.sx) || !std::isfinite(v.sy) || !std::isfinite(v.dx) || !std::isfinite(v.dy) ||
          !(v.w > 0 && v.w <= 64 && v.h > 0 && v.h <= 64))
        throw std::runtime_error("Invalid vector geometry");
      int x0 = std::max(0, int(std::floor((v.sx - v.w * 0.5f) / 16)));
      int y0 = std::max(0, int(std::floor((v.sy - v.h * 0.5f) / 16)));
      int x1 = std::min(columns - 1, int(std::floor((v.sx + v.w * 0.5f) / 16)));
      int y1 = std::min(rows - 1, int(std::floor((v.sy + v.h * 0.5f) / 16)));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) bins[y * columns + x].push_back(i);
    }
  }

  bool predict(const cv::Point2f &p, cv::Point2f &next) {
    if (vectors.empty() || p.x < 0 || p.x >= width || p.y < 0 || p.y >= height) return false;
    const int bx = int(p.x) / 16, by = int(p.y) / 16;
    int best = -1;
    float best_distance = std::numeric_limits<float>::max();
    for (auto i : bins[by * columns + bx]) {
      const auto &v = vectors[i];
      const float x = p.x - v.sx, y = p.y - v.sy;
      if (std::abs(x) > v.w * 0.5f || std::abs(y) > v.h * 0.5f) continue;
      if (x * x + y * y < best_distance) { best = int(i); best_distance = x * x + y * y; }
    }
    cv::Point2f flow;
    if (mode == "rect") {
      if (best < 0) return false;
      flow = cv::Point2f(vectors[best].dx, vectors[best].dy);
    } else {
      std::vector<float> dx, dy;
      std::vector<cv::Point2f> proposals;
      if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }
      for (int y = std::max(0, by - 1); y <= std::min(rows - 1, by + 1); ++y)
        for (int x = std::max(0, bx - 1); x <= std::min(columns - 1, bx + 1); ++x)
          for (auto i : bins[y * columns + x]) {
            if (seen[i] == epoch) continue;
            seen[i] = epoch;
            const auto &v = vectors[i];
            if ((p.x - v.sx) * (p.x - v.sx) + (p.y - v.sy) * (p.y - v.sy) > 24 * 24) continue;
            dx.push_back(v.dx); dy.push_back(v.dy);
            proposals.emplace_back(v.dx, v.dy);
          }
      if (dx.size() < 3) {
        if (best < 0) return false;
        flow = cv::Point2f(vectors[best].dx, vectors[best].dy);
      } else {
        std::sort(dx.begin(), dx.end()); std::sort(dy.begin(), dy.end());
        flow = cv::Point2f(dx[dx.size() / 2], dy[dy.size() / 2]);
        // Large local disagreement often means an occlusion/depth boundary.
        std::vector<float> deviation;
        for (const auto &proposal : proposals)
          deviation.push_back(std::abs(proposal.x - flow.x) + std::abs(proposal.y - flow.y));
        std::sort(deviation.begin(), deviation.end());
        if (deviation[deviation.size() / 2] > 8) return false;
      }
    }
    next = p + flow;
    return next.x >= 0 && next.x < width && next.y >= 0 && next.y < height;
  }
};

inline State &state() { static State s; return s; }
inline double percentile(std::vector<double> values, double fraction) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  return values[size_t(fraction * (values.size() - 1))];
}
} // namespace ov_codec_mv

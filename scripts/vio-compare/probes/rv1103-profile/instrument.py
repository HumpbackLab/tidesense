#!/usr/bin/env python3
"""Add per-frame timers to an isolated copy of the clean OpenVINS replay tree."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
schur = '--schur' in sys.argv[2:]
ov = root / 'third_party/open_vins'

def edit(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f'{path}: expected one occurrence of {old[:80]!r}')
    path.write_text(text.replace(old, new))

(ov / 'ov_core/src/utils/replay_profile.h').write_text(r'''#pragma once
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
namespace replay_profile {
using Clock = std::chrono::steady_clock;
enum Stage { Histogram, Pyramid, Detection, Flow, Normalize, Ransac, TrackOther,
             Clean, Triangulate, System, Compress, Ekf, Matching, Count };
inline std::array<double, Count> elapsed{};
inline bool enabled = false;
inline FILE *output = nullptr;
inline void init() {
  const char *path = std::getenv("OPENVINS_DETAIL_CSV");
  if (!path) return;
  output = std::fopen(path, "w");
  if (!output) { std::perror(path); std::exit(2); }
  enabled = true;
  std::fprintf(output, "timestamp_ns,initialized,measured,decode_ms,vio_ms,cpu_ms,start_ns,end_ns,histogram_ms,pyramid_ms,detection_ms,flow_ms,normalize_ms,ransac_ms,track_other_ms,msckf_clean_ms,msckf_triangulate_ms,msckf_system_ms,msckf_compress_ms,msckf_ekf_ms,matching_total_ms\n");
}
struct Chain {
  Stage stage;
  Clock::time_point last;
  explicit Chain(Stage value) : stage(value), last(enabled ? Clock::now() : Clock::time_point{}) {}
  void next(Stage value) {
    if (enabled) {
      auto now = Clock::now();
      elapsed[stage] += std::chrono::duration<double, std::milli>(now-last).count();
      last = now;
    }
    stage = value;
  }
  ~Chain() { next(stage); }
};
inline void write(long long ts, bool initialized, bool measured, double decode, double vio, double cpu,
                  Clock::time_point start, Clock::time_point end) {
  if (!enabled) return;
  std::fprintf(output, "%lld,%d,%d,%.9f,%.9f,%.9f,%lld,%lld", ts, initialized, measured, decode, vio, cpu,
      (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count(),
      (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());
  for (double value : elapsed) std::fprintf(output, ",%.9f", value);
  std::fputc('\n', output);
}
inline void finish() { if (output) { std::fclose(output); output = nullptr; } }
}
''')

klt = ov / 'ov_core/src/track/TrackKLT.cpp'
edit(klt, 'using namespace ov_core;', '#include "utils/replay_profile.h"\nusing namespace ov_core;')
edit(klt, '    // Histogram equalize', '    replay_profile::Chain profile(replay_profile::Histogram);\n    // Histogram equalize')
edit(klt, '    // Extract image pyramid', '    profile.next(replay_profile::Pyramid);\n    // Extract image pyramid')
edit(klt, '  int pts_before_detect = (int)pts_last[cam_id].size();', '  replay_profile::Chain profile(replay_profile::Detection);\n  int pts_before_detect = (int)pts_last[cam_id].size();')
old = '  perform_detection_monocular(img_pyramid_last[cam_id], img_mask_last[cam_id], pts_left_old, ids_left_old);\n  rT3 = boost::posix_time::microsec_clock::local_time();'
edit(klt, old, old + '\n  profile.next(replay_profile::Matching);')
edit(klt, '  assert(pts_left_new.size() == ids_left_old.size());', '  profile.next(replay_profile::TrackOther);\n  assert(pts_left_new.size() == ids_left_old.size());')
# The matching function has its own mutually exclusive flow/normalization/RANSAC timers.
edit(klt, '  // Now do KLT tracking to get the valid new points', '  replay_profile::Chain profile(replay_profile::Flow);\n  // Now do KLT tracking to get the valid new points')
edit(klt, '  // Normalize these points, so we can then do ransac', '  profile.next(replay_profile::Normalize);\n  // Normalize these points, so we can then do ransac')
edit(klt, '  // Do RANSAC outlier rejection (note since we normalized', '  profile.next(replay_profile::Ransac);\n  // Do RANSAC outlier rejection (note since we normalized')
edit(klt, '  cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0 / max_focallength, 0.999, mask_rsc);', '  cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0 / max_focallength, 0.999, mask_rsc);\n  profile.next(replay_profile::TrackOther);')

if schur:
    header = ov / 'ov_core/src/utils/replay_profile.h'
    text = header.read_text().replace('Matching, Count', 'Matching, SchurEliminate, Count')
    text = text.replace('msckf_', 'schur_').replace('schur_compress_ms', 'schur_cleanup_ms')
    text = text.replace('matching_total_ms\\n', 'matching_total_ms,schur_eliminate_ms\\n')
    header.write_text(text)

msckf = ov / ('ov_msckf/src/update/UpdaterSchurVINS.cpp' if schur else 'ov_msckf/src/update/UpdaterMSCKF.cpp')
edit(msckf, 'using namespace ov_core;', '#include "utils/replay_profile.h"\nusing namespace ov_core;')
edit(msckf, '  // Start timing', '  replay_profile::Chain profile(replay_profile::Clean);\n  // Start timing')
for number, stage in [(1, 'Triangulate'), (2, 'System'), (3, 'Compress'), (4, 'Ekf')]:
    old = f'  rT{number} = boost::posix_time::microsec_clock::local_time();'
    edit(msckf, old, old + f'\n  profile.next(replay_profile::{stage});')
if schur:
    edit(msckf, '  auto it3 = landmarkVec.begin();',
         '  profile.next(replay_profile::SchurEliminate);\n  auto it3 = landmarkVec.begin();')

bench = root / ('benchmarks/openvins_replay/replay.cpp' if schur else 'benchmarks/openvins_replay/openvins_replay_benchmark.cpp')
include = '#include "euroc_dataset.h"' if schur else '#include "imu_feed.h"'
edit(bench, include, include + '\n#include "utils/replay_profile.h"')
edit(bench, '  std::size_t imu_index = 0, eligible_frames = 0, processed = 0;', '  replay_profile::init();\n  std::size_t imu_index = 0, eligible_frames = 0, processed = 0;')
edit(bench, '    const auto decode_start = Clock::now();', '    replay_profile::elapsed.fill(0);\n    const auto decode_start = Clock::now();')
edit(bench, '    if (measure) {', '''    replay_profile::write(frame.timestamp_ns, vio.initialized(), measure,
        milliseconds(decode_start, decode_end), milliseconds(start, end),
        1000.0 * static_cast<double>(cpu_end - cpu_start) / CLOCKS_PER_SEC, start, end);
    if (measure) {''')
edit(bench, '  const auto alg = vio_replay::summarize(vio_ms), cpu = vio_replay::summarize(cpu_ms);', '  replay_profile::finish();\n  const auto alg = vio_replay::summarize(vio_ms), cpu = vio_replay::summarize(cpu_ms);')
print('Instrumented', root)

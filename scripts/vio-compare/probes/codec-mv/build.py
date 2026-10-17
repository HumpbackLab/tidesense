#!/usr/bin/env python3
"""Build isolated PC/FP64 replays, replacing only TrackKLT and replay adapter."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BASE = ROOT / 'install/openvins-backend-f32/host-fp64'
TRACK = ROOT / 'install/openvins-backend-f32/a53-fp64/base_source/ov_core/src/track'
OUT = ROOT / 'install/openvins-codec-mv/build'

def once(text, old, new):
    assert text.count(old) == 1, (old[:100], text.count(old))
    return text.replace(old, new)

def flags(target):
    lines = (BASE / f'build/CMakeFiles/{target}.dir/flags.make').read_text().splitlines()
    return sum((shlex.split(next(s.split('=', 1)[1] for s in lines if s.startswith(k + ' =')))
                for k in ['CXX_DEFINES', 'CXX_INCLUDES', 'CXX_FLAGS']), [])

def run(cmd, **kw):
    print(shlex.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), check=True, **kw)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (TRACK / 'TrackKLT.cpp').read_text()
    source = once(source, '#include "TrackKLT.h"', '''#include "TrackKLT.h"
#include <chrono>
#ifndef OPENVINS_CODEC_MV_EXPERIMENT
#define OPENVINS_CODEC_MV_EXPERIMENT 0
#endif
#ifndef OPENVINS_RANSAC_CAP_200
#define OPENVINS_RANSAC_CAP_200 0
#endif
#if OPENVINS_CODEC_MV_EXPERIMENT
#include "codec_mv.hpp"
#if OPENVINS_CODEC_MV_SHALLOW
#include "lk_mv_shallow.hpp"
#endif
#endif''')
    source = once(source, 'void TrackKLT::feed_new_camera(const CameraData &message) {', '''void TrackKLT::feed_new_camera(const CameraData &message) {
  // Match the current deployed 16x15 window, including old inline constructors.
#if OPENVINS_LK_WINDOW_16X15
  win_size = cv::Size(16, 15);
#endif''')
    # Trace IDs after all frontend acceptance gates, without changing tracker class layout.
    mono_anchor = '''  // Update our feature database, with theses new observations
  for (size_t i = 0; i < good_left.size(); i++) {
    cv::Point2f npt_l = camera_calib.at(cam_id)->undistort_cv(good_left.at(i).pt);'''
    source = once(source, mono_anchor, '''#if OPENVINS_CODEC_MV_EXPERIMENT && OPENVINS_CODEC_MV_SHALLOW
  auto &trace = ov_codec_mv::state();
  if (trace.tracks && cam_id == 0 && trace.lk_stats.path.size() == ids_left_old.size()) {
    for (size_t i = 0; i < ids_left_old.size(); ++i) {
      const bool retained = std::find(good_ids_left.begin(), good_ids_left.end(), ids_left_old[i]) != good_ids_left.end();
      std::fprintf(trace.tracks, "%llu,%u,%zu,%u,%u,%u,%u,%u\\n",
        static_cast<unsigned long long>(trace.current_ns), trace.frame_index-1, ids_left_old[i],
        unsigned(trace.lk_stats.path[i]), unsigned(trace.lk_stats.failure[i]),
        unsigned(trace.lk_stats.valid[i]), unsigned(mask_ll[i]), unsigned(retained));
    }
  }
#endif
''' + mono_anchor)
    before, matching = source.split('void TrackKLT::perform_matching', 1)
    matching = once(matching, '  // We must have equal vectors', '''#if OPENVINS_CODEC_MV_EXPERIMENT
  ov_codec_mv::state().lk_stats = ov_codec_mv::State::LKStats{};
#endif
  // We must have equal vectors''')
    matching = once(matching, '  // Now do KLT tracking to get the valid new points', '''#if OPENVINS_CODEC_MV_EXPERIMENT
  using Clock = std::chrono::steady_clock;
  const auto seed_begin = Clock::now();
  auto &mv = ov_codec_mv::state();
  std::vector<uchar> seeded(pts0.size(), 0);
  std::vector<cv::Point2f> proposals = pts1;
  if (mv.mode != "off" && id0 == 0 && id1 == 0) {
    for (size_t i = 0; i < pts0.size(); ++i) {
      cv::Point2f next;
      if (mv.predict(pts0[i], next)) {
        pts1[i] = proposals[i] = next;
        seeded[i] = 1;
      }
    }
  }
  const auto lk_begin = Clock::now();
#endif
  // Now do KLT tracking to get the valid new points''')
    call = '  cv::calcOpticalFlowPyrLK(img0pyr, img1pyr, pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit, cv::OPTFLOW_USE_INITIAL_FLOW);'
    matching = once(matching, call, '''#if OPENVINS_CODEC_MV_EXPERIMENT && OPENVINS_CODEC_MV_SHALLOW
  ov_codec_mv::match(img0pyr, img1pyr, pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit, seeded);
#else
''' + call + '\n#endif')
    matching = once(matching, '  // Normalize these points, so we can then do ransac', '''#if OPENVINS_CODEC_MV_EXPERIMENT
  const auto normalize_begin = Clock::now();
#endif
  // Normalize these points, so we can then do ransac''')
    matching = once(matching, '  // Do RANSAC outlier rejection', '''#if OPENVINS_CODEC_MV_EXPERIMENT
  const auto ransac_begin = Clock::now();
#endif
  // Do RANSAC outlier rejection''')
    original = '  cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0 / max_focallength, 0.999, mask_rsc);'
    matching = once(matching, original, '''#if OPENVINS_RANSAC_CAP_200
  cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0 / max_focallength, 0.999, 200, mask_rsc);
#else
''' + original + '\n#endif')
    matching = once(matching, '  // Copy back the updated positions', '''#if OPENVINS_CODEC_MV_EXPERIMENT
  const auto ransac_end = Clock::now();
  if (mv.detail && id0 == 0 && id1 == 0) {
    size_t ns = 0, nk = 0, ng = 0, nv = 0;
    std::vector<double> seed_errors, zero_errors;
    for (size_t i = 0; i < pts0.size(); ++i) {
      ns += seeded[i]; nk += mask_klt[i] != 0; ng += mask_out[i] != 0;
      nv += mv.lk_stats.path.size() == pts0.size() && mv.lk_stats.path[i] == 3 && mask_out[i];
      if (seeded[i] && mask_out[i]) {
        seed_errors.push_back(cv::norm(proposals[i] - pts1[i]));
        zero_errors.push_back(cv::norm(pts0[i] - pts1[i]));
      }
    }
    auto ms = [](Clock::time_point a, Clock::time_point b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::fprintf(mv.detail, "%llu,%zu,%zu,%zu,%zu,%zu,%.9f,%.9f,%.9f,%.9f,%.6f,%.6f,%.6f,%.6f,%zu,%zu,%zu,%.9f,%.9f,%zu,%zu,%zu,%zu,%.9f\\n",
      static_cast<unsigned long long>(mv.current_ns), pts0.size(), ns, nk, ng, seed_errors.size(),
      ms(seed_begin, lk_begin), ms(lk_begin, normalize_begin), ms(normalize_begin, ransac_begin),
      ms(ransac_begin, ransac_end), ov_codec_mv::percentile(seed_errors, .5),
      ov_codec_mv::percentile(zero_errors, .5), ov_codec_mv::percentile(seed_errors, .95),
      ov_codec_mv::percentile(zero_errors, .95), mv.lk_stats.shallow, mv.lk_stats.full,
      mv.lk_stats.retries, mv.lk_stats.shallow_ms, mv.lk_stats.full_ms,
      mv.lk_stats.verify_candidates, mv.lk_stats.verified, nv, mv.lk_stats.refreshed, mv.lk_stats.verify_ms);
  }
#endif
  // Copy back the updated positions''')
    (OUT / 'TrackKLT.cpp').write_text(before + 'void TrackKLT::perform_matching' + matching)
    shutil.copy2(TRACK / 'TrackKLT.h', OUT / 'TrackKLT.h')

    adapter = (ROOT / 'install/replay-20261012-084638/a53-mono-benchmark/run_record.cpp').read_text()
    adapter = once(adapter, '#include <chrono>', '#include <chrono>\n#include <cstdlib>\n#include "codec_mv.hpp"')
    adapter = once(adapter, '    ov_msckf::VioManager system(options);', '''    if (options.state_options.num_cameras != 1)
      throw std::runtime_error("Codec MV experiment currently requires mono");
    const char *ahead_env = std::getenv("OV_CODEC_IMU_AHEAD");
    const double imu_ahead = ahead_env ? std::stod(ahead_env) : 0.006;
    ov_msckf::VioManager system(options);''')
    adapter = once(adapter, 'camera_time + 0.006', 'camera_time + imu_ahead')
    adapter = once(adapter, 'cam_imu_timeoffset_s\\n', 'cam_imu_timeoffset_s,mv_prepare_ms\\n')
    adapter = once(adapter, '      system.feed_measurement_camera(image);', '''      ov_codec_mv::state().advance(time_ns, image.images[0].cols, image.images[0].rows);
      const auto prepared = std::chrono::steady_clock::now();
      const double mv_prepare_ms = std::chrono::duration<double, std::milli>(prepared - start).count();
      system.feed_measurement_camera(image);''')
    adapter = once(adapter, "<< cam_imu_timeoffset << '\\n';", "<< cam_imu_timeoffset << ',' << mv_prepare_ms << '\\n';")
    (OUT / 'run_record.cpp').write_text(adapter)
    defines = ['-DOPENVINS_CODEC_MV_EXPERIMENT=1', '-DOPENVINS_CODEC_MV_SHALLOW=1', '-DOPENVINS_LK_FILTER_VALID=1',
               '-DOPENVINS_LK_WINDOW_16X15=1', f'-DOPENVINS_CODEC_MV_SKIP_LK={int(args.skip_lk)}',
               '-I' + str(HERE), '-I' + str(TRACK), '-g1']
    compiler = shlex.split((BASE / 'build/CMakeFiles/run_record.dir/link.txt').read_text())[0]
    archiver = '/usr/bin/ar' if args.platform == 'host' else compiler.removesuffix('g++') + 'ar'
    run([compiler, *flags('run_record'), *defines, '-c', OUT / 'run_record.cpp', '-o', OUT / 'run_record.o'])
    library = OUT / 'libov_msckf_lib.a'
    shutil.copyfile(BASE / 'build/libov_msckf_lib.a', library)
    for cap in args.caps:
        run([compiler, *flags('ov_msckf_lib'), *defines,
             f'-DOPENVINS_RANSAC_CAP_200={int(cap == 200)}', '-c', OUT / 'TrackKLT.cpp', '-o', OUT / 'TrackKLT.cpp.o'])
        run([archiver, 'r', library, OUT / 'TrackKLT.cpp.o'])
        link = shlex.split((BASE / 'build/CMakeFiles/run_record.dir/link.txt').read_text())
        link = [str(OUT / 'run_record.o') if s.endswith('run_record.cpp.o') else
                str(library) if s == 'libov_msckf_lib.a' else s for s in link]
        link[link.index('-o') + 1] = str(OUT / f'run_record.r{cap}')
        run(link, cwd=BASE / 'build')
        if args.platform == 'a53':
            run([compiler.removesuffix('g++')+'strip', '-o', OUT/f'run_record.r{cap}.stripped', OUT/f'run_record.r{cap}'])
    (OUT / 'build-info.json').write_text(json.dumps(dict(base=str(BASE), tracker=str(TRACK),
        backend='FP64', window='16x15', lk_filter_valid=True, lk_iterations=30, lk_epsilon=.01,
        ransac_confidence=.999, macro='OPENVINS_CODEC_MV_EXPERIMENT=1',
        shallow_macro='OPENVINS_CODEC_MV_SHALLOW=1', shallow_levels=[0,1],
        shallow_default='disabled', patch_limit=20, refine_limit_px=6,
        skip_lk_macro=args.skip_lk, skip_lk_default='disabled',
        verification='64 bilinear zero-mean MAE and census samples; texture gate; periodic full LK'), indent=2) + '\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', choices=['host', 'a53'], default='host')
    parser.add_argument('--out', type=Path, default=OUT)
    parser.add_argument('--caps', nargs='+', type=int, choices=[1000,200], default=[1000,200])
    parser.add_argument('--skip-lk', action='store_true', help='Compile isolated H265 vector+patch verification path')
    args = parser.parse_args()
    BASE = ROOT / f'install/openvins-backend-f32/{args.platform}-fp64'
    OUT = args.out.resolve()
    main()

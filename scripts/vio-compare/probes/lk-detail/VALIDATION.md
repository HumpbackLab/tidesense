# lk-detail 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

### A53 results

All rows replayed the same first 800 frames of
`dataset-20260924-174756` at 640×400, 20 Hz stereo and 200 Hz IMU. The
board uses one OpenCV thread. The dynamic initializer MLE iteration count
was set to zero in **every** run because the original 50 ms initializer
limit prevents initialization on this A53. All runs initialized at frame 43
and had 757 initialized frames. `Camera` is the mean camera processing time
over initialized frames, excluding image-file loading. The LK, undistortion,
RANSAC and new stereo LK columns are mean summed stage times per replayed
frame. `Good` is mean retained left plus right features. `Pyramid=5` is the
requested level; OpenCV actually builds levels 0–4 for 640×400 and a 15×15
window.

| IMU guess | LK cap | Pyramid | RANSAC cap | Camera ms | Temporal LK ms | Undistort ms | RANSAC ms | Stereo LK ms | Good |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| No | 30 | 5 | 1000 | 90.77 | 22.63 | 1.70 | 32.95 | 3.67 | 110.8 |
| Yes | 30 | 5 | 1000 | 73.34 | 19.97 | 1.69 | 19.49 | 3.31 | 115.3 |
| Yes | 15 | 5 | 1000 | 76.06 | 19.28 | 1.70 | 18.75 | 2.83 | 115.9 |
| Yes | 30 | 3 | 1000 | 72.31 | 17.36 | 1.74 | 14.75 | 3.77 | 115.9 |
| Yes | 15 | 3 | 1000 | 64.66 | 16.49 | 1.71 | 14.92 | 2.96 | 117.2 |
| Yes | 30 | 5 | 200 | 64.34 | 19.84 | 1.65 | 11.33 | 3.27 | 114.7 |
| Yes | 15 | 3 | 200 | 62.06 | 16.39 | 1.68 | 9.79 | 2.94 | 116.6 |

IMU initial flow reduces OpenCV LK by only 2.7 ms/frame on this 800-frame
subset, but also reduces RANSAC by 13.5 ms/frame. The old whole-matching
timer included both. RANSAC has a long tail during rotation: at 5–8 rad/s,
the no-IMU run used about 119 ms/frame in RANSAC; IMU initial flow reduced
that to 52 ms/frame. With IMU initial flow and a 200-iteration RANSAC cap,
the same bin used 26 ms/frame, while mean good features changed from 115.3
to 114.7 overall. Frames with fewer than 40 good features increased from
39 to 42 of the 796 frames with `LK_RESULT` lines. The combined LK and
RANSAC caps had 34 such low-feature frames and provided the best measured
speed in this experiment, but did not meet the 50 ms frame period.

The separate OpenCV-instrumented 800-frame IMU run found 642,749 eligible
point-level LK loops. They averaged 4.71 iterations; 4.48% reached at least
15 iterations and 2.01% reached the 30-iteration cap. Mean iterations by
level 0–4 were 4.47, 4.16, 4.39, 4.83, and 5.67. This explains why reducing
the LK cap alone saved less than 1 ms/frame in temporal LK. The numbers
include temporal and new-feature stereo LK calls, but exclude points rejected
before the inner iteration loop. Do not use timing from this instrumented
binary: the histogram atomics and per-level output add overhead.

Every variant still diverged severely: first-to-last estimated position
distance ranged from about 27 m to 1.9 km, while the recording returned
within roughly 0.3 m of its starting point. These results support only
tracker timing and feature-retention comparisons. They do not establish
VIO accuracy or justify changing the production defaults. The initializer
change is another reason these trajectories are not a production accuracy
benchmark.

### Local UZH-FPV `indoor_forward_6` check

The same opt-in tracker changes were tested on the complete local UZH-FPV
`indoor_forward_6` replay using the existing x86-64 OpenVINS build and
official `uzhfpv_indoor` calibration. `run_fisheye.patch` adds the same IMU
initial-flow integration to the existing host replay entry point.
To reproduce, apply `openvins-core.patch` with `patch -p1` inside
`install/dataset-analysis-20260924-155813/pc/openvins/open_vins`, apply
`run_fisheye.patch` inside `install/fisheye-benchmark/openvins`, rebuild the
host `ov_msckf_lib` target, then run the existing `build_runner.py`.
`run_uzh_host.sh` runs all six rows below from the RAM-backed copy of the
1970 paired 640×480 images (about 30 Hz) and 500 Hz IMU. `evaluate_uzh.py`
evaluates all variants on the same 816 GT-covered image timestamps, applying
the official cam0 −16.684572 ms timestamp shift and independent SE(3)
alignment with fixed scale. The GT overlap is 30.15 s in the middle of the
69.6 s recording. Camera timing excludes image decoding and file I/O and
averages initialized frames; stage timings sum over all replayed frames.

| IMU initial flow | LK cap | Pyramid | RANSAC cap | Camera ms | Pure LK ms | RANSAC ms | Good L+R | ATE RMSE m |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| No | 30 | 5 | 1000 | 10.16 | 1.99 | 0.92 | 114.3 | 0.238 |
| Yes | 30 | 5 | 1000 | 9.45 | 1.98 | 0.93 | 115.8 | 0.205 |
| Yes | 30 | 3 | 1000 | 9.57 | 1.79 | 0.94 | 115.4 | 0.218 |
| Yes | 30 | 5 | 200 | 8.99 | 1.98 | 0.50 | 115.9 | 0.201 |
| Yes | 15 | 3 | 200 | 9.60 | 1.84 | 0.58 | 114.7 | 0.208 |
| No | 15 | 3 | 200 | 10.53 | 1.97 | 0.68 | 112.9 | 0.238 |

The unmodified parameter row reproduced the previous 0.23755 m ATE on
the same 816-frame interval exactly. Repeating the IMU plus RANSAC-200 row
produced identical pose values in all 1970 rows; its CPU timing varied by
roughly 0.25 ms across the two runs. The 200-iteration RANSAC cap cut its
own mean time from about 0.93 to 0.50 ms/frame without a measured ATE loss
in this sequence. Frames with fewer than 40 retained features increased
from 5 to 13, so the cap has a tracking tradeoff despite similar mean
feature counts. Lowering the LK pyramid/iteration caps did not improve
overall time or accuracy here. Unlike the TideSense recording, this sequence
has lower angular rates (95th percentile about 3.4 rad/s), and neither LK
nor RANSAC dominates the host PC runtime. These host measurements do not
predict the A53 wall time.

Detailed logs, trajectories, and the machine-readable common-frame
comparison are under the ignored local directory
`install/fisheye-benchmark/openvins/lk-detail-uzh/`.

### UZH-FPV at 10 Hz camera rate

`prepare_uzh_10hz.py` selects the closest stereo pair to each 100 ms grid
point from the same UZH replay. It produced 696 pairs over 69.5 s, an
actual mean of 10.0003 Hz, median gap 99.5 ms and maximum gap 133.6 ms.
All 500 Hz IMU samples are retained. The 10 Hz runs use the same official
camera calibration and time offset as the 30 Hz runs. Run the patched host
binary with `run_uzh_10hz.sh`, then `evaluate_uzh_10hz.py`.

The table uses the **same 302 GT-covered selected timestamps** for every
ATE calculation. It aligns each position trajectory independently to GT
using SE(3) with scale fixed. OpenVINS `q_GtoI` attitudes were compared
between the 10 and 30 Hz runs at all 629 common initialized timestamps
after a single initial global-frame rotation alignment. The GT orientation
is not used because the sequence's Leica orientation is less reliable than
its position.

| Configuration | 30 Hz position ATE m | 10 Hz position ATE m | 10-vs-30 Hz attitude p95 | 10 Hz initialized |
| --- | ---: | ---: | ---: | ---: |
| Original, no IMU LK guess | 0.237 | 0.352 | 1.66° | 629 / 696 |
| IMU guess, original LK/RANSAC | 0.204 | 0.356 | 1.19° | 629 / 696 |
| IMU guess, RANSAC 200 | 0.201 | 0.367 | **1.05°** | 629 / 696 |
| IMU guess, LK 15 / level 3 / RANSAC 200 | 0.209 | 0.397 | 1.17° | 629 / 696 |

For the IMU plus RANSAC-200 row, attitude difference median/p95/max/final
were 0.23°/1.05°/1.46°/0.99° over the full sequence. On the 34 selected
frames with gyro magnitude 3–5 rad/s, its attitude difference p95 was
0.87°. Repeating that 10 Hz run produced identical pose values in all
696 rows. Its average retained left+right features dropped from about
116 at 30 Hz to 86 at 10 Hz; 76 of 695 10 Hz result frames had fewer than
40 features, including 62 of the 302 GT-window frames. Thus the tested
10 Hz rate kept attitude close to the 30 Hz
estimate on this sequence, while position ATE worsened and visual tracking
margin shrank. This is a host-PC accuracy replay, not an A53 throughput
measurement or a ground-truth attitude evaluation. Raw results are in
`install/fisheye-benchmark/openvins/uzh-10hz/`.

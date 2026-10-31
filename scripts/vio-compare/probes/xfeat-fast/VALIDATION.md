# xfeat-fast 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

## FAST points with XFeat descriptors: offline probe

This tests the narrow change discussed for OpenVINS: retain FAST point detection
and replace pairwise temporal LK association with XFeat sparse descriptors.
It does **not** run the OpenVINS estimator or measure trajectory accuracy.

### Method

- Upstream XFeat: `verlab/accelerated_features` commit
  `e92685f57f8318b18725c5c8c0bd28c7fe188d9a`, official `xfeat.pt`.
- Equalize each grayscale image, find up to 200 FAST points in a 5-by-5 grid
  (FAST threshold 20). Use the same previous-frame points for both methods.
- LK: OpenCV pyramidal LK, 15-by-15 window, level 3, 15 iterations. XFeat:
  extract the official network's normalized descriptor map on each image,
  bicubic-sample descriptors at FAST positions, then use mutual nearest
  cosine matching with thresholds 0.82 and 0.70. This intentionally keeps
  FAST, so XFeat's own learned keypoint detector is not evaluated.
- Count matches that survive pixel-coordinate fundamental-matrix RANSAC
  (2 px, 0.99 confidence). This is a **rough geometry proxy**. It is not
  verified correspondence ground truth and can be degenerate during rotation.
- Sample 20 frame pairs from each of the bottom and top gyroscope-speed
  quartiles of each dataset. Evaluate left and right temporal pairs separately.
  The pairs are sampled in speed order, not uniformly in time. Each class
  contains 40 camera-pair observations. Times are host-PC single-thread CPU
  times; they do not predict A53 or TPU times. The network is recomputed for
  both images of each pair, so the reported per-camera-pair inference time
  includes two images; a streaming stereo implementation would cache the
  preceding descriptors and run two *new* images per stereo frame.

### Results

All counts below are per camera-pair observation. `F inliers` means matches
accepted by the diagnostic RANSAC. The threshold shown is cosine 0.70.

| Dataset and motion class | Mean gyro rad/s | LK matches | LK F inliers | XFeat matches | XFeat F inliers | XFeat beats LK on |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| TideSense low | 1.10 | 165.6 | 131.0 | 97.3 | 68.2 | 0 / 40 |
| TideSense high | 6.13 | 137.2 | 48.1 | 73.9 | 41.2 | 24 / 40 |
| UZH-FPV low | 0.02 | 189.8 | 188.0 | 133.3 | 121.7 | 0 / 40 |
| UZH-FPV high | 2.66 | 174.0 | 104.7 | 91.1 | 50.8 | 0 / 40 |

In TideSense high-motion samples, median F inliers were 32 for LK and 40
for XFeat. XFeat brought 9 of 40 camera-pair observations from LK's fewer
than 40 F inliers to at least 40. The converse happened in 5 of 40. At the
stereo-pair level, summing left and right F inliers, XFeat beat LK on 11 of
20 high-motion frames. At cosine 0.82, TideSense high-motion XFeat averaged
38.5 F inliers and beat LK on 19 of 40 camera pairs. Lowering the threshold
therefore helps some strong-motion frames but does not make full replacement
attractive in this sparse-match probe.

The TideSense host PC mean LK call took around 5 ms per camera pair. XFeat
network inference for two images took around 90–100 ms per camera pair at
640x400, excluding FAST and RANSAC. On UZH-FPV's 640x480 frames it took
around 140–170 ms. These are CPU/PyTorch prototype timings; the SG2002 TPU
results follow below.

**Interpretation:** A selective rescue path for frames where LK has very
few geometric inliers is worth investigating. A full LK replacement using
XFeat descriptors sampled at FAST points loses many ordinary-frame tracks,
and on UZH-FPV it also loses high-motion tracks. Whether rescued matches
improve VIO pose needs an actual OpenVINS trajectory run. This initial
diagnostic has no trajectory output; complete replay results follow below.

### Reproduce

```sh
git clone https://github.com/verlab/accelerated_features.git /tmp/tidesense-xfeat
git -C /tmp/tidesense-xfeat checkout e92685f57f8318b18725c5c8c0bd28c7fe188d9a
python3 -m venv --system-site-packages /tmp/tidesense-xfeat-venv
/tmp/tidesense-xfeat-venv/bin/pip install 'numpy<2'
/tmp/tidesense-xfeat-venv/bin/python scripts/vio-compare/probes/xfeat-fast/compare.py \
  --replay install/dataset-analysis-20260924-174756/replay \
  --xfeat /tmp/tidesense-xfeat \
  --out install/openvins-pipeline-20260925/xfeat-fast-tidesense-070.json \
  --pairs-per-class 20 --threshold 0.70
```

The upstream checkout includes `weights/xfeat.pt`. The output JSON has one
record per camera-pair observation and includes the individual timing and
count measurements. The UZH-FPV run uses
`install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay`
as `--replay`.

### SG2002 TPU feasibility gate

The narrow FAST-compatible XFeat graph was exported from the official weights
with `export_tpu.py`. It takes a **pre-normalized** 1x1x384x640 grayscale image
and outputs the 1x64x48x80 descriptor map. XFeat's own keypoint and heatmap
heads, and the dense-match refinement MLP, are excluded because the intended
test retains FAST. The split graph output matched the official PyTorch feature
map exactly on the export image (`max abs error = 0`). The ONNX graph has 20
convolutions, 18 ReLUs, two resizes, three adds, one average pool and shape
operations. Image resize, equalization, image-wide normalization, sparse
descriptor sampling and mutual-nearest matching remain outside the TPU graph.

TPU-MLIR 1.30.2 compiled both BF16 and INT8 for `cv181x`; 32 TideSense left
and right images sampled through the strong-motion recording were used for
INT8 calibration. The generated files are in the ignored local directory
`install/openvins-pipeline-20260925/xfeat-tpu/`. Model and runtime copies are
also on the board in `/mnt/data/xfeat-tpu/`.

| Variant | cvimodel size | TideSense TPU `Forward` per image | Two images per stereo frame |
| --- | ---: | ---: | ---: |
| BF16 | 1.4 MB | 26.263 ms (30 calls) | 52.526 ms |
| INT8 | 723 KB | 9.052 ms (100 calls, real normalized input) | 18.104 ms |

The board used the SDK's `model_runner` and `libcviruntime` 1.4.0. The timing
excludes camera capture, FAST detection, A53 normalization, descriptor
sampling, matching, geometric rejection and OpenVINS estimation. One INT8
output was compared with the same image's ONNX FP32 descriptor map: global
cosine similarity 0.9950, mean absolute element error 0.1409. This is a
basic numeric check, not a feature-match accuracy test.

The fastest measured A53 camera-processing experiment on the TideSense replay
took 62.06 ms/frame, including 16.39 ms temporal LK and 2.94 ms new-stereo LK
(see the neighboring `lk-detail` probe). Even granting zero-cost replacement
matching and removing both LK calls, adding 18.104 ms of INT8 TPU inference
gives an optimistic **60.83 ms/frame** before A53 preprocessing and TPU
contention. Its original test used a reduced RANSAC cap and still had
severely divergent trajectories, so this is a timing lower bound, not a viable
VIO configuration. It does not meet a 50 ms / 20 Hz frame period. At 10 Hz the
network alone is within a 100 ms budget; the complete integration was measured
later in this document.

The key commands used were:

```sh
source /tmp/tidesense-xfeat-venv/bin/activate
python scripts/vio-compare/probes/xfeat-fast/export_tpu.py \
  --xfeat /tmp/tidesense-xfeat \
  --image install/dataset-analysis-20260924-174756/replay/left/23041789764390.pgm \
  --out install/openvins-pipeline-20260925/xfeat-tpu
cd install/openvins-pipeline-20260925/xfeat-tpu
model_transform.py --model_name xfeat_sparse_backbone --model_def xfeat_backbone.onnx \
  --input_shapes '[[1,1,384,640]]' --mlir xfeat_sparse_backbone.mlir
python3 ../../../scripts/vio-compare/probes/xfeat-fast/make_calibration.py \
  --replay ../../dataset-analysis-20260924-174756/replay --out calibration --count 32
run_calibration.py xfeat_sparse_backbone.mlir --data_list calibration/list.txt \
  --input_num 32 --chip cv181x -o xfeat_sparse_backbone_cali_table
model_deploy.py --mlir xfeat_sparse_backbone.mlir --quantize INT8 --chip cv181x \
  --calibration_table xfeat_sparse_backbone_cali_table \
  --model xfeat_sparse_backbone_int8.cvimodel
```

#### Complete learned detector timing

For the follow-up question about replacing the whole front end,
`export_tpu.py --with-heads` exported the feature map, learned keypoint logits,
and reliability map together. The split PyTorch graph matched all three
official outputs exactly on the export image. TPU-MLIR 1.30.2 compiled the
three-output INT8 model for `cv181x` using the same 32 calibration images.
The 875 KB cvimodel ran on the board in **11.416 ms per image** over 100
inferences with a real normalized input, or **22.832 ms per stereo pair**.
This excludes A53 image normalization, heatmap softmax/NMS, top-k and spatial
distribution, descriptor sampling, temporal and stereo matching, track IDs,
geometric rejection, and the OpenVINS estimator.

The prior 550-frame A53 stage profile measured 11.06 ms for stereo OpenCV
pyramid plus derivatives and 3.31 ms for feature top-off detection. Mixing
those stage means with the fastest 800-frame experimental camera mean above
gives the *rough serial optimistic estimate*
`62.06 - 16.39 - 2.94 - 11.06 - 3.31 + 22.832 = 51.192 ms/frame`.
The profiles are from different replay subsets and the 62.06 ms result used
an experimental RANSAC cap with divergent trajectories, so 51.2 ms is not a
measured integrated pipeline. The missing A53 postprocessing makes a direct
serial 20 Hz implementation unlikely at these settings. A pipeline that runs
the TPU for the next stereo frame while the A53 finishes the prior estimator
update could hide some inference latency, but this has not been implemented
or benchmarked. Likewise, any estimator or geometric-gating changes need a
trajectory-accuracy test. No complete XFeat/OpenVINS front end was built.

#### Complete XFeat front-end replay experiment

The subsequent experiment replaces OpenVINS `TrackDescriptor`'s FAST/ORB
detector and descriptors with the three-output INT8 XFeat cvimodel. The A53
normalizes a grayscale image at the model's fixed size (640x384 for TideSense,
640x480 for UZH-FPV), runs XFeat on the CV181x TPU, applies
the official 65-way softmax and 5x5 NMS, ranks by detection probability times
bilinearly interpolated reliability, samples PyTorch-compatible bicubic descriptors,
and matches mutual nearest normalized
descriptors. Geometric RANSAC and the OpenVINS estimator remain on the A53.
The implementation is in `XFeatTPU.cpp/.h`, `track-descriptor.patch`,
`imu-init.patch`, and `xfeat-cmake.patch` here;
the executable checkout is the ignored
`install/vio-compare-20260924-121320/openvins/open_vins` tree. CVI headers
needed without the SDK's shadow OpenCV headers are in `cvi-headers/`. Apply the
three patches to the matching OpenVINS checkout, copy `XFeatTPU.cpp/.h` into
`ov_core/src/track/`, and configure CMake with
`-DCVI_RUNTIME_INCLUDE_DIR=<path to cvi-headers>` and
`-DCVI_RUNTIME_LIBRARY=<path to libcviruntime.so>`. The replay executables and
dataset configurations used for the measurements reside in the ignored local
experiment tree and are not part of this probe.

The experiment uses `OPENVINS_XFEAT_MODEL` to select the TPU model; without it
the original descriptor tracker uses FAST/ORB. The cosine gate is 0.70 and
the current stereo pair's RANSAC match permits a track ID to continue when
one camera has a temporal match. Both dataset configs use `num_pts=600` and
`init_max_features=800`, since OpenVINS divides the latter over two cameras
and the original value of 50 limited XFeat to 25 candidates per camera.
These changes affect the test configuration and must be stated when comparing
against the default KLT tracker.

UZH-FPV has no one-second tracks under the default visual-disparity
initialization rule. The separate `config-uzh-imuinit` variant sets
`init_max_disparity=0`, `init_dyn_use=true`, and uses the small
`imu-init.patch` (`OPENVINS_XFEAT_IMU_INIT=1`) to start from an IMU stationary
window. It does not change XFeat detection, matching, or the VIO update after
initialization. The normal TideSense recording initializes with the default
visual-disparity rule.

The A53 binary needs the GCC 11 glibc loader and runtime from the toolchain;
they are staged in ignored `install/xfeat-board-eval/runtime`. On the board,
the runner, runtime and configs are in `/mnt/data/xfeat-eval`, and the model
and CVI runtime are in `/mnt/data/xfeat-tpu`. Invocation:

```sh
cd /mnt/data/xfeat-eval
OPENVINS_XFEAT_MODEL=/mnt/data/xfeat-tpu/xfeat_full_frontend_480_int8.cvimodel \
OPENVINS_XFEAT_IMU_INIT=1 \
  ./runtime/ld-linux-aarch64.so.1 \
  --library-path ./runtime:/mnt/data/xfeat-tpu/lib \
  ./run_fisheye config-uzh-imuinit/estimator_config.yaml \
  /mnt/data/public-vio-benchmark/uzh-fpv-indoor6 xfeat-uzh-full-poses.csv 0
```

`evaluate.py` computes SE(3) aligned positional ATE without scale on the
GT-covered portion of the UZH trajectory. TideSense's own recording has no
per-frame GT, so only the estimated first-to-last displacement can be compared
with the user's reported return to within about 0.5 m of the start.

UZH-FPV `indoor_forward_6` completed all 1970 stereo frames on TideSense A53
with a 640x480 INT8 TPU model calibrated on 32 FPV images. Both XFeat and the KLT control used the same
IMU-stationary initialization variant and first produced a pose at frame 65.
The GT covers the same 816 initialized frames for both runs (30.154 s).

| A53 run | Mean / P95 camera call | Position ATE RMSE / P95 | Last GT-frame error |
| --- | ---: | ---: | ---: |
| Full XFeat front end | 493.7 / 612.5 ms | 0.890 / 1.425 m | 1.555 m |
| OpenVINS KLT, same initialization | 93.8 / 174.4 ms | 0.247 / 0.344 m | 0.346 m |

The two trajectories were independently aligned to GT with rotation and
translation only, without scale; the comparison uses identical timestamps.
All values are from complete replays, without real-time frame dropping. XFeat
was 3.61x worse in ATE RMSE and 5.26x slower in average camera-call time in
this configuration. A separate PC KLT baseline with the default visual
initialization gave 0.238 m ATE RMSE over the same GT-covered interval. These
numbers are experimental and do not establish which component of detection,
INT8 quantization, descriptor matching or track-ID association caused the
accuracy loss. The initial 640x384 FPV run used different interpolation math
and gave 1.100 m ATE RMSE; it is superseded by this corrected run. Full CSV
and logs are in ignored `install/xfeat-board-eval/`
and `/mnt/data/xfeat-eval/`.

The corrected 640x384 TideSense replay also completed all 1425 usable stereo
frames. XFeat first initialized at frame 94 and produced 1331 poses. Its mean
and P95 camera-call times were 476.5 and 581.9 ms. The estimated distance
from first to last pose was **2688.5 m**, versus **124.1 m** from the earlier
KLT A53 replay (1332 poses, first initialized at frame 93, mean 68.4 ms).
The user reported physically returning within about 0.5 m of the start.
Neither estimate is a valid closure, and the XFeat variant is much worse.
The XFeat displacement rose smoothly: about 32 m at 20 s, 184 m at 30 s,
517 m at 40 s, and 1092 m at 50 s after its first pose. All CSV positions
are finite, and the last ten displacements rise monotonically; the 2688 m
figure is not a single corrupted endpoint. This recording has no framewise
GT, so it cannot yield ATE. The first-to-last displacement only tests the
user's approximate return-to-start condition.

These runs establish the performance of the *current XFeat plus OpenVINS
descriptor-tracking integration*. They do not establish XFeat's standalone
matching accuracy or prove that a learned front end is unsuitable for VIO.
The sparse front end uses a 0.70 cosine gate, 400 candidates per camera during
initialization and 300 after, and a one-sided temporal match may preserve an
ID after the current stereo match passes RANSAC. Each of these choices can
affect VIO accuracy and would need a controlled ablation before assigning the
cause of the drift.

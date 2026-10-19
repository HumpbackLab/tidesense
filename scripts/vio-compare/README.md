# TideSense VIO comparison adapters

Run computer-side commands from the project root. Board-side commands use
the deployed paths on the device.

This directory contains the **version-controlled TideSense code** used to
cross compile and replay VINS-Mono, VINS-Fusion, and OpenVINS on the SG2002 A53.
It also contains a [host-only ov_SchurVINS comparison](ov-schurvins/README.md)
on UZH-FPV, TUM-VI, and a TideSense recording.
The upstream projects are pinned in [sources.lock](sources.lock). Their full
checkouts, CMake build trees, static libraries, recordings, logs, and
trajectories live under `install/vio-compare-20260924-121320/`, which this SDK
ignores in Git. The detailed board results are in that directory's
[REPORT.md](../../install/vio-compare-20260924-121320/REPORT.md).

For live TideSense cameras/IMU with an FP64 backend and optional micro-ROS pose output,
see [OpenVINS live startup and ROS 2 subscription](openvins/README-live.md).

| Project | Maintained code here | Pinned upstream checkout in `install/` |
| --- | --- | --- |
| VINS-Mono | `vins-mono/standalone/` ROS-free left-camera/IMU adapter and CMake build, plus `export_tum.py` and board script | `vins-mono/upstream/` |
| VINS-Fusion | `vins-fusion/offline.cpp`, CMake build, ROS header shim, Kalibr converter, board scripts, trajectory tools, and [ROS removal patch](patches/vins-fusion-offline.patch) | `vins-fusion/VINS-Fusion/` |
| OpenVINS | `openvins/run_record.cpp`, Kalibr converter, A53 toolchain, board script, [static offline build patch](patches/openvins-offline.patch), and [headless replay patch](patches/openvins-headless.patch) | `openvins/open_vins/` |
| ov_SchurVINS | `ov-schurvins/` host replay adapters, evaluator, and [host build patch](patches/ov-schurvins-host.patch) | `ov-schurvins/open_vins/` |

Upstream license notices remain in their respective checkouts. These adapters
are experiment code; they do not replace the SDK's SchurVINS production build.

## Sync and build

From the SDK root:

```sh
scripts/vio-compare/sync.sh
scripts/vio-compare/build.sh mono
scripts/vio-compare/build.sh fusion
scripts/vio-compare/build.sh openvins
scripts/vio-compare/build.sh ov-schurvins-host
```

`sync.sh` verifies the four pinned upstream revisions, applies the maintained patches
once, and copies the maintained adapter files to the ignored build workspace.
It downloads a checkout if it is absent and refuses to change a checkout at a
different revision. `build.sh` uses the SDK's GCC 11.3 AArch64 toolchain,
static OpenCV/glog from `install/schurvins-gcc11-static/stage`, Ceres 1.14 from
the comparison workspace, and for OpenVINS the cross-compiled Boost 1.74
libraries in `openvins/deps/`. Run `scripts/schurvins/build.sh` first if the
OpenCV/glog stage is missing. The comparison-specific Ceres and Boost stages
are preserved under `install/`; `build.sh` reports their absence rather than
silently using host libraries. `JOBS=2` is the default parallel build limit.

The VINS-Mono adapter currently embeds the camera and IMU values from
`cali/tidesense-imucam3-kalibr-20260924-101247` in `standalone/main.cpp` and
`standalone/parameters.cpp`. Rebuild it after changing those values. Generate
the other two project configs from a Kalibr result:

```sh
work=install/vio-compare-20260924-121320
kalibr=cali/tidesense-imucam3-kalibr-20260924-101247
python3 "$work/vins-fusion/generate_config.py" \
  "$kalibr/imucam-camchain-imucam.yaml" "$kalibr/imucam-imu.yaml" \
  "$work/vins-fusion/rotated" --rotate-right \
  --board-output-path /mnt/data/vio-compare/vins-fusion/rotated
python3 "$work/openvins/make_config.py" --kalibr-dir "$kalibr"
```

The TideSense right camera is rotated 180 degrees. VINS-Fusion and OpenVINS
rotate its PGM input and use a matching virtual-camera calibration. The
recording's stereo timestamps already include the cam0/IMU time offset, and
its historical IMU preprocessing is retained for that comparison. New captures contain nominal raw SI and use one configured intrinsic model.
VINS-Mono reads only the left camera.

## Replay artifact

The board copy is `/mnt/data/vio-compare/`. The replay scripts there read
`/mnt/data/vio-record-20260924-121320` and serialize runs through
`/mnt/data/vio-compare/run.lock`. This recording is marked `INCOMPLETE` due to
its writer queue, but its committed prefix has 744 complete stereo pairs and
8169 IMU rows. The source and output checks are in the ignored comparison
workspace's report; large recordings and trajectories are intentionally not
committed to the SDK.

## OpenVINS headless replay optimization

### Default test precision

Routine OpenVINS tests use the **original FP64 backend**. The isolated
`probes/backend-f32/build_variant.sh` defaults to `fp64`; the live build excludes
both mixed and full FP32 experiments. Those experiments require explicit opt-in.
Always state the precision used when reporting timings.

### Default LK window

OpenVINS now defaults to a **16×15** grayscale LK window. The maintained
[patch](patches/openvins-lk-window.patch) is applied by `sync.sh`, and both the
standard and precision-variant builds expose `OPENVINS_LK_WINDOW_16X15`, default
`ON`. To restore the upstream 15×15 window, rebuild with:

```sh
OPENVINS_LK_WINDOW_16X15=OFF scripts/vio-compare/build.sh openvins
```

The compile definition is public because `TrackKLT` has an inline constructor;
the library and its consumers must use the same window setting. The 16-pixel
width removes the per-row scalar tail in OpenCV's NEON LK main loop. On A53
UZH-FPV, 16×15 reduced full camera-call time by 8.8% with a small ATE increase;
see the [window comparison](probes/lk-window/README.md). That complete comparison
used FP64 covariance with local FP32 matrix multiplication. Full FP32 covariance
with 16×15 failed a covariance-diagonal check in this dataset.

### Skip visualization work

The upstream VIO manager re-triangulates active tracks and draws an image on
every camera update for ROS visualization. The standalone `run_record` adapter
does not read these outputs. To omit that work, build with:

```sh
OPENVINS_SKIP_ACTIVE_VIZ=ON scripts/vio-compare/build.sh openvins
```

The option defaults to `OFF`. It changes only the OpenVINS library used by the
offline adapter; builds that need `get_active_tracks()` or
`get_active_image()` should leave it off. Changing the option requires a
rebuild. The 2026-09-25 board profile and benchmark are saved under
`install/openvins-perf-20260925/`.

On the SG2002 A53, using the 744-frame valid prefix of
`vio-record-20260924-121320` (417 initialized frames), this option reduced
post-initialization camera-call time from 90.07 to 75.13 ms/frame (16.6%) and
full replay time from 86.44 to 80.37 seconds. All 744 output rows matched
exactly apart from the `compute_ms` column. Both builds used the same camera
and IMU data and `num_opencv_threads: 1`; these results are for offline replay,
not a live 20 Hz throughput claim.

The baseline A53 `perf` cycle samples were dominated by OpenCV KLT tracking
(23.89%) and Scharr derivatives (11.03%). Active-track drawing contributed
at least 7.78% through `addWeighted` and `copyMask8uC3`; the skipped
triangulation also costs time. With the option on, drawing disappears from the
hotspot list and KLT plus Scharr account for 41.64% of the remaining samples.
The profile includes both initialization and steady-state frames. Further
optimization should focus on the KLT tracker and EKF matrix work, with a
trajectory and tracking-quality check for any parameter or algorithm change.

### SG2002 IVE feasibility

The current SG2002 SDK cannot directly replace OpenCV's pyramidal LK with an
IVE call. `CVI_IVE_IOC_LKOpticalFlowPyr` exists as an ioctl number, but there
is no public `CVI_IVE_LKOpticalFlowPyr` implementation, no ioctl dispatch
case, and no cv181x HAL function for it. The HAL declaration is inside
`#if 0`. The board has `/dev/cvi-ive`, and its running kernel exposes
`cvi_ive_Resize` but no LK implementation in `/proc/kallsyms`. The official
CV180x/CV181x IVE API guide also does not list LK optical flow.

IVE Resize and programmable Sobel are implemented. Sobel's 5x5 mask can
represent a 3x3 Scharr kernel, but OpenCV's LK expects its own interleaved
derivative pyramid and border behavior. A compatible bridge would need
physical buffers, image copies or shared camera buffers, cache maintenance,
and result-format conversion. In the optimized replay profile, pyramid
`PyrDownVecH/V` consumed 3.84% of sampled cycles, versus 28.05% in LK and
13.59% in Scharr. Offloading Resize alone therefore has limited potential;
measure end-to-end cost and pixel equality before adding a production path.

An A53 probe using a recorded 640x400 TideSense frame put the 3x3 Scharr
coefficients in the center of IVE Sobel's 5x5 mask. Both S16 output planes
matched OpenCV Scharr exactly for every non-border pixel at all five pyramid
sizes; the outermost pixel ring differed. With cached physical buffers,
copy/flush + IVE + output sync + scalar S16C2 interleave took about 3.18,
0.88, 0.34, 0.17, and 0.12 ms for levels 0 through 4, or about 4.7 ms per
camera. A same-board OpenCV 3.4.20 benchmark measured 5.33 ms to build the
five-level pyramid with derivatives versus 2.57 ms without them: about 2.76
ms for the entire CPU derivative path. Simply substituting IVE Sobel is thus
slower before implementing OpenCV-compatible borders and pyramid integration.
Using non-cached IVE outputs made the CPU interleave alone about 79 ms at
640x400, so cached output buffers are essential to any future experiment.
The reproducible probe source, board binaries, images, and numeric output are
under `install/ive-scharr-probe/` (ignored build workspace).

IVE's SAD operates on fixed 4x4, 8x8, or 16x16 image blocks; NCC returns a
single image-pair statistic. Neither exposes OpenVINS KLT's sparse feature
list, subpixel image/gradient interpolation, or per-feature iterative update.
A CPU KLT rewrite could use IVE for preprocessing, but the measured Sobel
path offers no current latency benefit. A real hardware LK port would need a
new driver and user API for the disabled LK block, followed by accuracy and
end-to-end timing validation.

### TPU feasibility

SG2002 also exposes `/dev/cvi-tpu0`. The CV181x TPU has convolution kernels,
and `cviruntime` accepts a physical tensor address and supports asynchronous
forward calls. A Scharr convolution model is possible in principle. It must
preserve signed 16-bit gradients, border behavior, and OpenCV's interleaved
`CV_16SC2` pyramid layout after the TPU output conversion. The measured A53
OpenCV derivative increment is only 2.76 ms per 640x400 camera image across
all five levels; any TPU path must beat that including transfers and layout
conversion. No TPU model or board timing for this operation has been made.

With `ENABLE_CPU_FUNC`, the current runtime registers `grid_sampler` and gather operations as CPU
functions (`cviruntime/src/common/model.cpp`), so expressing KLT's sparse,
iterative subpixel sampling in a model would still execute crucial work on
the A53. A TPU model for Scharr alone does not offload KLT's feature-wise
Gauss-Newton loop. Replacing the tracker with a learned TPU frontend is a
different algorithm and needs separate accuracy and latency validation.

## EKF mixed-precision probe

The opt-in [`ekf-f32` probe](probes/ekf-f32/README.md) packs the measured
covariance columns and computes `P·Hᵀ` in FP32/NEON while retaining the EKF
state and covariance in FP64. On TideSense A53, its 744-frame replay reduced
mean camera processing time from 51.15 to 47.30 ms/frame; the same recording
produced matching initialization counts and a 0.012 mm final position
difference against the original arithmetic. Build with
`OPENVINS_EKF_MA_EXPERIMENT=1` to enable it.

## FP32 backend experiment

The separate [`backend-f32` probe](probes/backend-f32/README.md) ports the
FP32 backend conversion from the external `fpga_vio` project, retaining the original outlier
gates. On the ordinary A53 replay, FP32 plus packed `P Hᵀ` reduced backend
time from 34.11 to 21.70 ms and camera time from 51.25 to 44.31 ms. However,
both full FP32 variants exited on negative covariance diagonals during the
strong-motion recording. Keep this option disabled by default; see the
probe documentation for isolated builds, UZH-FPV results, and numerical limits.

## LK failed-track filtering

The [LK filter](probes/lk-filter/README.md) excludes failed LK tracks before
normalization and RANSAC, then maps the rejection mask back to the original
feature indices. It is enabled by default in both standard and precision
builds through `OPENVINS_LK_FILTER_VALID=ON`; rebuild with `OFF` to disable.
IMU initial flow and shallow tracking with retries were removed after the
FPV experiments. Historical measurements are retained in the filter directory.

## Optional RANSAC iteration cap

Only `OPENVINS_RANSAC_CAP_200` is retained from the mono frontend experiment.
It defaults to OFF. Build with `OPENVINS_RANSAC_CAP_200=ON` to limit the
fundamental-matrix RANSAC to 200 iterations; OFF uses the original OpenCV
default of 1000. Confidence stays 0.999 and LK settings stay unchanged.
See [usage and measurements](probes/ransac-cap/README.md).

## Codec motion-vector initial flow experiment

The separate [PC codec-MV experiment](probes/codec-mv/README.md) uses causal,
single-reference H.264 motion vectors to seed the existing full-pyramid LK
tracker, followed by unchanged geometric rejection and FP64 estimation.
It defaults to disabled and keeps codec extraction costs separate from VIO timing.

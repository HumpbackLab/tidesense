# SchurVINS on UZH-FPV indoor_forward_6 (host PC, 2026-09-28)

This experiment uses the existing 1970 stereo pairs and 500 Hz IMU replay.
The camera-to-IMU time shift is applied once in the input CSV. The GT covers
816 camera frames over 30.15 s. All runs disable micro-ROS and prediction.
Images are read from the same RAM disk. The diagnostics patch was applied only
to a detached temporary SchurVINS worktree; the SDK and board were not changed.

## Results

| Native SchurVINS run | Visual observation standard deviation | GT position ATE RMSE, SE(3) | Zero-valid-observation pose logs | GT-segment mean compute |
|---|---:|---:|---:|---:|
| Existing uninstrumented RAM run | 2 px | 15.89 m | 237 | 6.72 ms/frame |
| Instrumented control | 2 px | 18.19 m | 442 | 7.13 ms/frame |
| Instrumented A/B | 3 px | 5.04 m | 81 | 6.56 ms/frame |
| Instrumented A/B | 6 px | 2.93 m | 35 | 6.74 ms/frame |
| Instrumented 6 px repeat | 6 px | 2.93 m | 35 | 6.01 ms/frame |

The two 6 px pose CSV files are byte identical. Instrumentation changes
execution timing and the unstable 2 px trajectory, so compare the instrumented
runs with each other. The previous OpenVINS common-frame result on this
sequence was 0.238 m ATE across the same 816 GT frames. Increasing visual
noise improves SchurVINS substantially but does not close that gap. The 6 px
value is a diagnostic setting, not a calibrated camera-noise estimate.

## Where failure starts

For the instrumented 2 px control, after alignment using only the first 2 s:

| GT time | Position error | Frontend matches | Current-state observations before rejection | Kept after rejection | Mean pre-update reprojection residual |
|---:|---:|---:|---:|---:|---:|
| 2.01 s | 0.31 m | 240 | 192 | 117 | 1.8 px |
| 3.00 s | 0.87 m | 240 | 194 | 105 | 1.8 px |
| 4.00 s | 1.47 m | 240 | 169 | 80 | 2.4 px |
| 8.01 s | 4.41 m | 147 | 98 | 27 | 4.9 px |
| 10.00 s | 4.03 m | 182 | 76 | 2 | 10.9 px |
| 19.99 s | 14.87 m | 159 | 69 | 0 | 14.2 px |

Thus initial drift precedes collapse of the visual frontend. Later, the
frontend can still report 100-240 matches while the backend keeps few or none.
From 15-30 s, the median kept count is zero; 65% of frames keep zero.
Several late frames have pre-update residuals above 40 px and produce
single-frame pose corrections of 2-4 m. The run still reports
`tracking=1970/1970`.

The code path explains the mismatch. `FrameHandlerStereo::processFrame`
forces a keyframe when matches or valid optimized observations fall below 20,
without changing the tracking stage. `SchurVINS::Backward` calls `Solve3`
before `RemoveOutliers`; residuals above the later 4 px rejection threshold
therefore enter the current update. `Solve3` has Huber weighting but no
pre-update hard rejection. The A/B runs show that the fixed 2 px observation
weight is a major contributor. The early drift with 80-117 retained
observations means feature shortage alone is insufficient to explain the
failure. The remaining 2.93 m ATE at 6 px leaves frontend association,
measurement model, and estimator consistency to investigate.

## Reproduction

The patch `instrumentation.patch` adds buffered CSV output and an optional
`SCHUR_FPV_OBS_DEV` override to the native build. It writes:

- `SCHUR_FPV_FRONTEND_CSV`: image time, reprojection trials and matches.
- `SCHUR_FPV_BACKEND_CSV`: current features, observations in the solve,
  residuals above 4 px, mean/max residual, observations kept after rejection,
  and position/rotation change from prediction to update.

The patch targets SchurVINS commit `afd180b`, before the mitigation switches
below were added. Apply it in a separate checkout to reproduce the instrumented
control:

```bash
git -C SchurVINS worktree add --detach ../install/fpv-diagnosis-source afd180b
git -C install/fpv-diagnosis-source apply \
  "$PWD/scripts/vio-compare/probes/fpv-diagnosis/instrumentation.patch"
```

Results are in `install/fisheye-benchmark/schurvins/uzh-indoor-forward-6/`
under `fpv-diagnostics-precise`, `fpv-obs3`, `fpv-obs6`, and
`fpv-obs6-repeat`. Each contains poses, diagnostic CSVs, log, and
`evaluation.json`. The existing OpenVINS comparison is
`install/fisheye-benchmark/openvins/uzh-common-comparison-shm.json`.

## Sequential mitigation experiment

The following rows use the same instrumented host binary and RAM image
input. Each row adds the named step to the previous accepted configuration.
ATE uses all 816 GT frames unless coverage is shown.

| Step | Runtime setting added | Full-frame ATE | Zero-valid-observation logs | Result |
|---|---|---:|---:|---|
| Baseline | none | 18.19 m | 442 | Reference |
| 1. Pre-update rejection | 40 px current-frame landmark gate | 4.98 m | 177 | Better |
| 2. Stereo geometry | 10 px calibrated epipolar gate | 4.16 m | 150 | Small improvement; repeated pose file identical |
| 3. Tracking health | suppress pose when valid observations <20 | internal 4.16 m | 150 | 1187/1970 poses published, 273/816 GT frames |
| 4a. Angular-rate uncertainty | 3 ms model, capped at 6 px | 4.82 m | 81 | Worse full-frame ATE; health gate publishes 1235/1970, 283/816 GT frames |
| 4b. Angular-rate uncertainty | 6 ms model, capped at 6 px | 4.16 m | 168 | No accuracy gain |

Step 1 threshold sensitivity: 8 px yielded 18.65 m ATE and 20 px yielded
12.81 m. Step 2 sensitivity: 2 px yielded 15.29 m and 5 px yielded 41.18 m.
These strict gates discard useful high-rate observations. At 10 px, the
stereo gate rejected 384 pairs across the run. Health gating does not alter
the internal pose: every emitted pose from step 3 is byte identical to the
same timestamp in step 2. Its emitted-subset ATE is 4.37 m, which must not be
compared with full-frame ATE as an accuracy improvement.

The effective visual standard deviation in step 4 is
`min(max(base, 6), hypot(base, focal_px * angular_rate_rad_s *
uncertainty_ms / 1000))`. The 3 ms value is a trial coefficient, not a
measured camera/IMU timestamp error. The more modest but inconsistent
step-4 result is why the option remains disabled by default.

The SDK implementation exposes four independent, default-off runtime options:
`SCHUR_VISUAL_PREGATE_PX`, `SCHUR_STEREO_EPIPOLAR_PX`,
`SCHUR_HEALTH_GATE=1`, and `SCHUR_VISUAL_TIME_UNCERTAINTY_MS`.
An optional `SCHUR_VISUAL_RATE_START_RAD_S` delays the extra uncertainty
until angular speed exceeds that value. The staged diagnostic tests used
values 40, 10, 1, and 3 respectively. These parameters are experiment
settings for local replay; board validation is still required before
selecting defaults for TideSense.

## Clean SDK build cross-check

The diagnostic binary writes multiple CSV streams inside the estimator. A
separate clean SDK build has no estimator CSV instrumentation and gives
different trajectories, confirming that this 2 px case is sensitive to
execution conditions. Its sequential full-trajectory results are:

| Configuration | ATE RMSE | Zero-valid-observation logs | Output coverage |
|---|---:|---:|---:|
| All options disabled | 10.01 m | 225 | 1970/1970 |
| Step 1: pre-update 40 px gate | 12.50 m | 334 | 1970/1970 |
| Steps 1+2: add 10 px stereo gate | 3.51 m | 101 | 1970/1970 |
| Steps 1+2+3: health gate | internal 3.51 m | 101 | 1255/1970, including 270/816 GT frames |
| Steps 1+2+4: angular model 1 ms | 15.95 m | 236 | 1970/1970 |
| Steps 1+2+4: angular model 3 ms | 22.07 m | 319 | 1970/1970 |
| Steps 1+2+4: 3 ms, onset 2 rad/s | 6.36 m | 208 | 1970/1970 |

The step-3 emitted poses are byte identical to the same timestamps from
steps 1+2. Its emitted-subset ATE is 2.83 m, but it publishes only 270 of
816 GT frames, so this is not a comparable accuracy improvement.

**Decision:** keep each switch opt-in. Step 1 alone is not a reproducible
gain across instrumented and clean builds. Steps 1+2 together improve this
dataset, but remain far behind OpenVINS. Step 4 is unhelpful or harmful for
the tested coefficients and should not be enabled on TideSense from these
results. Step 3 provides honest output validity, with low coverage in the
hardest section; it does not reinitialize a lost trajectory.

## Matched IMU noise and estimator stability (2026-10-17)

The local `indoor_forward_6_replay/imu.csv` contains 34,815 measurements.
Its median timestamp interval is 1,999,855 ns and its full-span mean rate is
500.000008 Hz. `calibration.yaml` also specifies `imu_rate: 500`. This is
the dataset rate; TideSense's live SC7U22 configuration uses 200 Hz.

A major configuration difference was present in the previous comparison:

| Continuous-time IMU parameter | Existing SchurVINS YAML | Existing OpenVINS `uzhfpv_indoor/kalibr_imu_chain.yaml` |
|---|---:|---:|
| Gyroscope noise density | 0.05 | 0.00016968 |
| Accelerometer noise density | 0.1 | 0.002 |
| Gyroscope bias random walk | 0.00004 | 0.000019393 |
| Accelerometer bias random walk | 0.002 | 0.003 |

The matched-noise experiment copies these four values into a **new** SchurVINS
YAML. All other calibration fields, images, IMU samples, timestamps, frontend
settings, and visual noise remain the same. The OpenVINS YAML's `update_rate:
200` is not copied: the measured dataset rate remains 500 Hz. These are
OpenVINS's existing benchmark settings, not a new sensor-noise calibration.
They should not be transferred to SC7U22 without a separate measurement.

The optional `estimator-stability.patch` adds two independent, default-off
backend experiments, targeting local SchurVINS commit `fefa113`:

- `SCHUR_STABLE_UPDATE=1`: diagonalize the symmetric Schur Hessian, whiten its
  observable subspace, and solve an identity-noise innovation using LLT. The
  original compressed innovation `H P H^T + H` inherits the Hessian's gauge
  nullspace. Whitening excludes eigenvalues at or below
  `max(1e-10, lambda_max * 1e-10)` and rejects a materially indefinite Hessian.
  The covariance uses the Joseph update. This does not implement FEJ.
- `SCHUR_PIXEL_RESIDUAL=1`: use each camera's calibrated distorted-pixel
  projection and Jacobian in the body update, landmark EKF update, and
  reprojection rejection. The original normalized-plane error multiplied by
  one focal length is not a uniform pixel-noise model for a fisheye camera.
  Invalid active projections reject the point before Hessian accumulation.

All final FPV runs emit 1970/1970 poses and evaluate the same 816 GT frames.
SE(3) alignment fits rotation and translation, with scale fixed to one; neither
scale nor time offset is fitted. No health gate, pre-update residual gate,
stereo epipolar gate, or angular-rate uncertainty is enabled.

<!-- STABILITY_RESULTS_START -->
| Final native configuration | ATE RMSE (m) | Zero-observation logs | Repeated poses |
|---|---:|---:|---|
| Original settings | 23.447629 | 332 | Byte identical |
| Stable update | 5.099440 | 161 | Byte identical |
| Stable update + pixel residual | 4.820533 | 12 | Byte identical |
| Matched IMU noise only | 0.810299 | 0 | Byte identical |
| Matched noise + stable update | 0.632302 | 1 | Byte identical |
| Matched noise + stable update + pixel residual | 0.493473 | 0 | Byte identical |

The configuration-only change is the largest improvement. The combined
backend experiment reduces the remaining error further, but does not match
the existing OpenVINS reference. Earlier exploratory builds gave different
trajectories; only the final single-binary comparisons above are used here.
The stable-update-only run remains divergent at 5.10 m; better matrix
conditioning alone does not establish estimator accuracy.
<!-- STABILITY_RESULTS_END -->

The final-binary TUM-VI `room1` regression uses its own original IMU
calibration and all 2821/2821 GT frames:

| Configuration | ATE RMSE (m) | Zero-observation logs |
|---|---:|---:|
| Original settings | 0.163946 | 0 |
| Stable update | 0.195431 | 0 |
| Stable update + pixel residual | 0.155017 | 0 |

The stable update alone regresses TUM-VI accuracy. The combined change does
not regress this sequence in the measured run, but two sequences do not
establish a general default. Start with the dataset configuration comparison;
keep the backend patch opt-in. Final TUM outputs are in
`install/fpv-stability/final-tum/`.

A separate check with the existing **unpatched** native executable gives
30.233278 m with the original noise and 0.810299 m with matched noise.
Both settings repeat byte identically. Thus the configuration-only benefit
does not depend on the experimental backend patch. Original-noise divergent
trajectories remain build/allocation sensitive; do not compare their spread
as normal accuracy variation. These outputs are in
`install/fpv-stability/clean-noise/`. A committed snapshot with hashes and
all final metrics is [`results-20261017.json`](results-20261017.json).

The earlier OpenVINS result is 0.23755 m on the same 816 GT frames. It is a
reference, not a controlled backend comparison: the OpenVINS configuration
also enables intrinsic and camera/IMU time-offset estimation, and uses FEJ.
The [OpenVINS FEJ documentation](https://docs.openvins.com/fej.html) explains
how it preserves the unobservable VINS subspace. Neither FEJ nor online
calibration is added by this experiment.

### Reproduce the configuration comparison

`replay_ab.py` needs Python 3, NumPy and PyYAML. It preserves the original
calibration, writes a matched copy in a new output directory, clears inherited
`SCHUR_*` experiment settings, and executes variants sequentially. Each run
records its command, explicit environment, input and binary/library hashes,
poses, logs, timing, and evaluation. Missing output frames are an error rather
than an opportunity to report a lower subset ATE.

From the SDK root, an existing native binary can run the configuration-only
comparison without the experimental patch:

```bash
probe=scripts/vio-compare/probes/fpv-diagnosis
data=install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay
python3 "$probe/replay_ab.py" \
  --binary install/dataset-analysis-20260924-155813/pc-schurvins/build/schurvins_stereo \
  --calibration "$data/calibration.yaml" --stereo "$data/stereo.csv" \
  --imu "$data/imu.csv" --groundtruth "$data/groundtruth.csv" \
  --matched-imu install/fisheye-benchmark/openvins/uzhfpv_indoor/kalibr_imu_chain.yaml \
  --output install/fpv-noise-comparison --variants baseline matched-imu --repeats 2
```

For the backend experiments, use a separate checkout:

```bash
git -C SchurVINS worktree add --detach ../install/fpv-stability-source fefa113
git -C install/fpv-stability-source apply \
  "$PWD/scripts/vio-compare/probes/fpv-diagnosis/estimator-stability.patch"
scripts/vio-compare/probes/fpv-diagnosis/build_host.sh \
  install/fpv-stability-source install/fpv-stability-build
install/fpv-stability-build/schurvins_estimator_update_test \
  scripts/schurvins/synthetic.yaml "$data/calibration.yaml" \
  install/fisheye-benchmark/schurvins/tumvi-room1/calibration.yaml
```

The host build reuses the existing PC dependency workspace. Set
`SCHUR_HOST_DEPS_ROOT` if it is elsewhere. It does not build against A53
libraries. Tests cover rank-deficient measurement compression, equivalence to
a raw-measurement Kalman update with a singular cloned-state prior, gauge
nullspace preservation, posterior PSD, invalid input, pixel residual scale,
and finite-difference Jacobians for both cameras of each supplied rig.

Repeat the previous replay command with
`--binary install/fpv-stability-build/schurvins_stereo`, a new output directory,
and `--variants baseline stable pixel-stable matched-imu matched-imu-stable
matched-imu-pixel-stable` to reproduce the complete comparison. The measured
runs used identical images copied to `/dev/shm`; the corresponding CSV files
and outputs are in `install/fpv-stability/`. RAM paths must be populated again
after a reboot. Production source, board software, and board defaults are
unchanged; the patch remains an isolated experiment.

## Full UZH-FPV suite (2026-10-17)

The frozen [`uzh-sequences.json`](uzh-sequences.json) records the official
catalogue: 28 Snapdragon stereo sequences (16 with public GT), plus three
SplitS/T265 sequences. SplitS's published camera/IMU calibration contains
only one camera; it is recorded as unsupported by the native stereo entry,
without fabricating a second camera or a stereo baseline.

The suite holds the four matched-noise values and estimator switches fixed
across every stereo sequence. It runs original SchurVINS, matched-noise-only
SchurVINS, and matched noise plus both backend experiments. Each of the four
camera mounting groups uses its own official calibration. Public-GT sequences
also run the existing OpenVINS host reference binary and estimator preset,
with that group's camera calibration. OpenVINS retains its existing online
intrinsic/time-offset estimation settings.

Preparation pairs images by exact original timestamps, rather than row numbers.
For example, forward 10's equal row numbers are one frame apart; its exact
timestamp intersection supplies 2125 stereo pairs. Unmatched images are
counted per camera. It converts PNG to mono8 PGM with no resizing or
rectification, and applies cam0's official camera/IMU offset exactly once. All algorithms consume
the same corrected stereo timestamps; OpenVINS starts its offset at zero in
that corrected clock. Pairs outside the available IMU interval are counted
explicitly and omitted from every method. Archive member reads check ZIP CRC;
HTTP ranges and archive SHA256 fingerprints are recorded by the downloader.
Images are staged one sequence at a time in RAM and removed after its runs.

Full-frame SchurVINS ATE requires complete finite output. The cross-method
comparison intersects initialized pose timestamps with the eligible GT frame
set, reports that coverage explicitly, and scores all available methods on
the same timestamps. Failed methods remain failures in the table. Alignment
is SE(3), scale fixed to one, with no GT-fitted timestamp correction.
Sequences without public GT report frame coverage, finite output, and backend
observation statistics; these do not establish trajectory accuracy.

Run or resume downloads and the sequential replay loop in separate terminals:

```bash
probe=scripts/vio-compare/probes/fpv-diagnosis
python3 "$probe/uzh_download.py" --cache install/uzh-fpv-suite/cache --workers 32
python3 "$probe/uzh_suite.py" \
  --cache install/uzh-fpv-suite/cache --output install/uzh-fpv-suite/results \
  --binary install/fpv-stability/build/schurvins_stereo \
  --noise-reference install/fisheye-benchmark/openvins/uzhfpv_indoor/kalibr_imu_chain.yaml \
  --openvins-binary install/fisheye-benchmark/openvins/run_fisheye \
  --openvins-config install/fisheye-benchmark/openvins/uzhfpv_indoor/estimator_config.yaml
python3 "$probe/uzh_report.py" install/uzh-fpv-suite/results
```

The downloader resumes completed ranges. The replay loop resumes completed
sequences and records process failures/timeouts per variant. Large archives,
images and raw logs stay under ignored `install/uzh-fpv-suite/`. The protocol
uses host replay; it does not measure A53 throughput or validate board defaults.

<!-- UZH_SUITE_RESULTS_START -->
### Full-suite results and counterexamples

All 28 stereo sequences completed all three native runs (84/84), with full
input-frame coverage and finite poses. This is execution coverage, not an
accuracy claim. Sixteen sequences have public GT; twelve do not. The three
SplitS sequences are unsupported by this stereo entry because their
published camera/IMU calibration contains only one camera.

For a direct comparison with OpenVINS, use the **same 15 sequences** where
all four methods produced initialized trajectories and covered the complete
eligible GT frame set:

| Method | Median sequence ATE RMSE (m), same 15 sequences |
|---|---:|
| Original | 1.574718 |
| Matched IMU noise | 1.105677 |
| Matched noise + backend patch | 0.909849 |
| OpenVINS reference | 0.274286 |

Across all **16** GT sequences, the three native medians are 1.534660 m, 1.086597 m, 0.912177 m.
The backend combination improves over noise-only on 13/16 sequences and
regresses on three outdoor sequences. It should remain opt-in. OpenVINS
processes `outdoor_45_1` but never initializes with the frozen reference
preset; its ATE is unavailable rather than zero.

| Sequence | GT frames | Original (m) | Noise (m) | Noise + backend (m) | OpenVINS (m) |
|---|---:|---:|---:|---:|---:|
| indoor_45_12 | 1076/1076 | 1.344 | 1.106 | 1.030 | 0.235 |
| indoor_45_13 | 1036/1036 | 1.358 | 1.248 | 0.910 | 0.187 |
| indoor_45_14 | 1025/1025 | 5.144 | 1.144 | 0.934 | 0.274 |
| indoor_45_2 | 1264/1264 | 1.052 | 0.483 | 0.477 | 0.327 |
| indoor_45_4 | 1153/1153 | 0.874 | 0.410 | 0.391 | 0.297 |
| indoor_45_9 | 578/578 | 1.099 | 0.509 | 0.446 | 0.338 |
| indoor_forward_10 | 819/819 | 5.726 | 2.175 | 0.638 | 0.236 |
| indoor_forward_3 | 1332/1332 | 0.607 | 0.605 | 0.552 | 0.357 |
| indoor_forward_5 | 529/529 | 2.737 | 2.359 | 1.101 | 0.158 |
| indoor_forward_6 | 816/816 | 6.795 | 0.669 | 0.527 | 0.230 |
| indoor_forward_7 | 1735/1735 | 1.984 | 1.068 | 0.915 | 0.368 |
| indoor_forward_9 | 766/766 | 4.740 | 4.133 | 2.767 | 0.262 |
| outdoor_45_1 | 506/506 | 0.564 | 1.020 | 1.458 | Not initialized |
| outdoor_forward_1 | 1164/1164 | 1.495 | 1.372 | 1.059 | 0.551 |
| outdoor_forward_3 | 2223/2223 | 998.891 | 1.142 | 1.225 | 1.038 |
| outdoor_forward_5 | 444/444 | 1.575 | 0.455 | 0.532 | 0.171 |

`outdoor_45_1`, `outdoor_forward_3`, and `outdoor_forward_5` were repeated
with the same native command paths, input preparation, and environment.
All nine repeated pose files are byte identical to their first runs.
Both copies are retained under each sequence's `verification/` directory.

For no-public-GT sequences, the following values are the fraction of backend
pose-optimization logs with **zero valid observations**. They are diagnostics,
not position-error measurements. All runs retain 100% pose-output coverage;
a finite pose can still be an inertially drifting trajectory.

| No-public-GT sequence | Original zero-obs logs | Noise zero-obs logs | Noise + backend zero-obs logs |
|---|---:|---:|---:|
| indoor_45_1 | 0.0% (0/2645) | 0.0% (0/2645) | 0.0% (0/2645) |
| indoor_45_11 | 18.0% (257/1428) | 1.6% (23/1435) | 0.3% (5/1435) |
| indoor_45_16 | 0.5% (6/1293) | 0.0% (0/1293) | 0.0% (0/1293) |
| indoor_45_3 | 0.0% (0/2074) | 0.0% (0/2074) | 0.0% (0/2074) |
| indoor_forward_11 | 0.5% (12/2202) | 0.0% (0/2202) | 0.0% (0/2202) |
| indoor_forward_12 | 0.0% (0/1649) | 0.1% (2/1649) | 0.0% (0/1649) |
| indoor_forward_8 | 0.0% (0/4547) | 0.0% (0/4547) | 0.0% (0/4547) |
| outdoor_45_2 | 3.9% (74/1910) | 0.0% (0/1910) | 0.0% (0/1910) |
| outdoor_forward_10 | 40.0% (947/2367) | 0.0% (0/2914) | 0.0% (0/2914) |
| outdoor_forward_2 | 0.0% (0/2368) | 0.0% (0/2368) | 0.0% (0/2368) |
| outdoor_forward_6 | 32.2% (685/2129) | 0.5% (11/2151) | 0.1% (2/2151) |
| outdoor_forward_9 | 18.5% (398/2150) | 0.0% (0/2363) | 0.0% (0/2363) |

The frozen results, binary/configuration fingerprints, repeated-run metrics,
and all 36 archive fingerprints are in
[`uzh-suite-results-20261017.json`](uzh-suite-results-20261017.json). Raw
artifacts remain in `install/uzh-fpv-suite/results/`. Images are restaged from
the cached ZIPs when needed; RAM image links are intentionally temporary.

Repeat the three counterexamples without discarding their first outputs:

```bash
python3 scripts/vio-compare/probes/fpv-diagnosis/uzh_verify.py \
  --results install/uzh-fpv-suite/results --cache install/uzh-fpv-suite/cache \
  --binary install/fpv-stability/build/schurvins_stereo \
  --noise-reference install/fisheye-benchmark/openvins/uzhfpv_indoor/kalibr_imu_chain.yaml
```

Use a new `--output` directory for a fresh complete suite. The replay loop
invalidates native cached runs when their binary or configuration fingerprint
changes. These results support a useful reduction in the tested drift errors,
with clear outdoor counterexamples; they do not justify changing TideSense
board defaults.
<!-- UZH_SUITE_RESULTS_END -->

## TideSense dataset-20261012-084638 (2026-10-17)

Replayed the recording from `/mnt/data/dataset-20261012-084638` on the host,
using the same FP64 experimental binary as the UZH suite. All seven original
files, including `frames.bin`, match the board copy by SHA256. The archive has
885 stereo pairs over 44.303382 s and 8868 IMU samples: measured rates are
19.95333 Hz and 199.53337 Hz. Every exported PGM pixel and every exported IMU
value was checked against the original archive before replay.

This comparison keeps the recording snapshot's radial-tangential camera
calibration, original sensor orientation, 10.797086 ms export timestamp
offset, zero additional replay offset, and SC7U22 noise parameters. These four
noise values already match the earlier OpenVINS configuration for this
recording, so there is no separate noise-matching variant. The later
equidistant recalibration used in other OpenVINS experiments is not used in
this table. Prediction, micro-ROS, and other experimental gates are disabled.

| Backend switches | First-to-last position distance (m) | Maximum pose-difference speed (m/s) | Zero-observation logs / update logs |
|---|---:|---:|---:|
| Original | 332.060 | 40.587 | 84 / 861 |
| Stable update only | 306.071 | 37.752 | 86 / 851 |
| Calibrated pixel residual only | 334.271 | 40.824 | 85 / 833 |
| Both | 339.686 | 41.306 | 69 / 840 |

All eight runs (four variants, twice each) consumed all 885 frames and output
885 finite poses with matching input timestamps and unit quaternions. Each
variant's two pose files are byte-identical; repeated runs use identical
command paths and environment settings. Finite outputs and full coverage do
not imply a correct trajectory. The recording has no external ground truth;
first-to-last distance is only a return-to-start drift reference, not ATE.
All four trajectories retain severe late drift. In the original / combined
runs, displacement from the first pose is 0.673 / 0.703 m at 20 s,
18.947 / 18.709 m at 30 s, and 182.281 / 186.621 m at 40 s. These two backend
changes do not resolve this recording's failure and remain disabled by default.

Frozen metrics and fingerprints are in
[`tidesense-results-20261017.json`](tidesense-results-20261017.json).
Raw logs, poses, repeated outputs, calibration snapshot, and the trajectory
distance plot are under `install/fpv-stability/tidesense-20261012-084638/`.
This is a host offline accuracy diagnostic; no firmware or device executable
was changed and no A53 performance claim is made.

```bash
install/dataset-test-venv/bin/python \
  scripts/vio-compare/probes/fpv-diagnosis/recording_ab.py \
  --source install/replay-20261012-084638/dataset-20261012-084638 \
  --recording install/replay-20261012-084638/record \
  --binary install/fpv-stability/build/schurvins_stereo \
  --output install/fpv-stability/tidesense-20261012-084638-new
```

### Replay with the user-specified device YAML

Fetched `/mnt/data/schurvins-gcc11-static-micro-ros/schurvins.yaml` from the
device and passed its unmodified contents directly as the replay calibration.
Its board SHA256 and the downloaded file SHA256 both equal
`6c92e82235b89d04cc50ad0753bc0fe535331dec159004ce965a966d7eadc073`.
This also exactly matches the recording's original `calibration.yaml`.
Calling the previous calibration an older calibration was inaccurate: parsed
camera, IMU, and initialization parameters match the previous `replay.yaml`.
The difference is that `replay.yaml` has its live-camera offset reset to zero.
The stereo CSV already includes the 10.797086 ms offset, and the offline
adapter ignores `live_camera_time_offset_ns`, so it is applied once.

| Backend switches | First-to-last position distance (m) |
|---|---:|
| Original | 323.517 |
| Stable update only | 331.147 |
| Calibrated pixel residual only | 311.174 |
| Both | 343.569 |

All four variants were run twice: all eight runs consumed all 885 input
pairs and output 885 finite poses with full timestamp coverage. Every pair
of repeated pose files is byte-identical. All variants still exhibit severe
drift. These new outputs differ from the earlier normalized-YAML runs; the
cross-run numerical difference has not been isolated and is not evidence of
a calibration improvement. There is no external ground truth or ATE.

The frozen record is
[`tidesense-device-config-results-20261017.json`](tidesense-device-config-results-20261017.json).
Raw artifacts are in `install/fpv-stability/tidesense-device-config-20261017/`.
To reproduce, use the previous command with
`--calibration install/fpv-stability/device-schurvins-20261017.yaml` and a new
output directory. The downloaded YAML is kept unmodified. No device programs
or calibration files were modified.

## IMU model and loss-of-constraints diagnosis (2026-10-17)

The main recording's runaway drift is strongly affected by the gyro model,
followed by loss of usable multi-frame visual constraints. This is supported
by controlled replays, not just the difference between two estimators.
It does not establish an independently measured physical sensor calibration.

### OpenVINS control with exactly the same device camera calibration

Rebuilt the OpenVINS camera YAML from the user-specified SchurVINS YAML:
same intrinsics, radial-tangential distortion, camera/IMU extrinsics, and
already-aligned camera timestamps. The existing OpenVINS adapter rotates
the right image by 180 degrees; its virtual camera extrinsics, principal
point, and tangential coefficients were transformed consistently. A numeric
projection check verifies this transformation to below 1e-10 px. No extra
camera time shift or online camera calibration is enabled.

Both controls use the same existing FP64 executable, 1 px visual sigma,
IMU-based startup stationary gate, recorded IMU noise, and `imu0.model:
kalibr`. Only `calib_imu_intrinsics` changes; gyro acceleration sensitivity
remains disabled. Thus the prior equidistant camera configuration is not a
confound in this new comparison.

| OpenVINS, same cameras and 1 px visual noise | First-to-last distance (m) | Maximum displacement (m) | Maximum state speed (m/s) |
|---|---:|---:|---:|
| Fixed IMU intrinsics | 232.501 | 232.501 | 30.434 |
| Online IMU intrinsics | 0.972 | 0.972 | 2.060 |

All four runs (two repeats each) consume all 885 frames and output the same
647 post-startup normal poses, beginning at 11.927864 s. Repeated pose values
are identical after excluding the timing column. No external ground truth is
available: first-to-last distance is not ATE.

### SchurVINS input-only ablations

The uninstrumented binary from the earlier experiments is reused. Camera
YAML, visual settings, IMU noise coefficients, frame order, and all input
timestamps stay fixed. Only a new IMU input CSV changes. Static gyro bias is
computed from the first ten seconds. The intrinsic matrices below come from
the existing OpenVINS equidistant/sigma3 online run on this **same** recording;
they are rounded solver estimates, not an independent calibration.

OpenVINS uses `omega = R_GYROtoIMU * Dw * (omega_raw - bg)` and
`accel = Da * (accel_raw - ba)` in this Kalibr parameterization. The diagnostic
preprocessing applies the constant matrices to the sensor input; SchurVINS
continues estimating its own bias in the transformed coordinates. Scalar IMU
noise parameters are held fixed for this controlled experiment.

| SchurVINS input change | First-to-last distance (m) | Maximum displacement (m) | Maximum pose-difference speed (m/s) |
|---|---:|---:|---:|
| None | 323.517 | 323.517 | 39.429 |
| Subtract static gyro bias only | 332.003 | 332.003 | 40.557 |
| Gyro diagonal scale only | 71.780 | 71.780 | 10.094 |
| Gyro scale and axis coupling, Dw | 5.188 | 5.188 | 3.973 |
| Gyro-frame rotation only | 362.832 | 362.832 | 42.634 |
| Full gyro model, R * Dw | 1.072 | 4.429 | 3.196 |
| Accelerometer model only, Da | 316.534 | 316.534 | 38.698 |
| Full gyro and accelerometer models | 10.154 | 10.154 | 2.219 |

The gyro diagonal correction is approximately +1.51%, -0.56%, +0.46%.
The full Dw also has off-diagonal entries 0.0040, -0.0019, and -0.0025,
plus a small estimated gyro-frame rotation. Bias-only propagation has no
state dimensions to represent these rate-dependent errors. A constant bias
can match a stationary gyro, but cannot represent `(I - M) * omega` as the
angular rate and axis change.

These ablations identify gyro scale/axis effects as an important contributor,
and show that accelerometer correction or initial gyro bias alone does not
rescue this recording. The 1.072 m endpoint must not be read as a solved
trajectory: it reaches 4.429 m displacement and still has long intervals
without usable multi-frame constraints. The same-camera OpenVINS online
trajectory stays within 0.972 m of its first normal position.

### When the Schur solver loses visual constraints

A separate worktree at `install/imu-divergence/source/` adds per-update state,
prediction, covariance traces, and pre-update observation diagnostics. The
instrumented raw/corrected replays reproduce the uninstrumented endpoints
to within 1e-6 m. Compare instrumented controls with each other.

During the first ten seconds, recorded mean gyro is approximately
`[-0.000560, 0.006902, 0.006903]` rad/s; the filter reaches
`[-0.000594, 0.006834, 0.006861]` rad/s. The early static segment is therefore
not failing simply because the configured initial bias is zero.

| Raw-IMU Schur backend interval | Calls with no usable solve observations | Median current observations after rejection | Maximum state speed (m/s) |
|---|---:|---:|---:|
| 12–20 s | 20 / 157 | 121 | 0.272 |
| 20–22 s | 28 / 39 | 11 | 1.711 |
| 22–24 s | 40 / 40 | 4 | 2.712 |
| 24–30 s | 118 / 120 | 6 | 6.279 |
| 30–40 s | 191 / 191 | 3 | 28.305 |
| 40 s–end | 49 / 49 | 0 | 39.493 |

`solve_obs` counts eligible multi-frame observations actually accumulated in
`Solve3`, after point/status/window eligibility checks. It differs from the
current-frame `nObs` log and from frontend feature counts. A zero residual
diagnostic when `solve_obs == 0` means **no measurements**, not a perfect fit.

The measured angular speed reaches 903.5 deg/s. Near 20 s, usable constraints
collapse as rapid rotation begins. Once the raw run has almost no usable
constraints, bias updates largely stop and inertial propagation continues
while the frontend forces new keyframes and remains in the tracking stage.
This explains why finite pose output and `tracking=885` do not indicate a
working estimator. Gyro correction improves later constraint reacquisition,
but many backend calls still have no eligible observations. Failure recovery
and frontend/backend robustness remain unresolved.

A separate stereo-PnP/gyro comparison, using existing tracked observations,
reduces the median rotation disagreement at 24–30 s from 0.311 to 0.227 deg
after gyro correction. This is supporting consistency evidence, not ground
truth; the p95 does not improve uniformly.

### Integration and transfer counterexamples

Schur propagation holds the left-end IMU sample over each interval. A
default-off midpoint experiment averages the interval endpoints, including
the interpolated image-time endpoint. It does not rescue the recording:

| Instrumented propagation | First-to-last distance (m) |
|---|---:|
| Original raw-IMU propagation | 323.517 |
| Midpoint only | 303.030 |
| Midpoint + stable visual update | 316.263 |
| Full gyro correction + original propagation | 1.072 |
| Full gyro correction + midpoint | 1.757 |

Freezing an earlier gyro estimate from just before 20 s gives 202.633 m on
the main recording. Transferring the final correction to the independent
`dataset-20261012-003648` recording also fails: 757.611 m raw versus
895.002 m corrected. Both transfer variants consume all 829 frames and output
the same 610 post-initialization poses, starting at 10.975768 s. These are
first-to-last references without ground truth. The favorable in-sample matrix
must not be promoted to production calibration.

The supported next implementation target is a gyro scale/axis calibration
model with appropriate online estimation or independently validated offline
calibration, plus recovery after visual constraints are lost. The earlier
Joseph update and calibrated pixel residual experiments do not add those
IMU model dimensions and do not address the demonstrated mechanism.

### Reproduction and artifacts

[`imu_model_probe.py`](imu_model_probe.py) runs input-model and integration
ablations with saved commands, matrix values, hashes, repeated poses, and
full input/output coverage. Frozen results are in
[`imu-debug-results-20261017.json`](imu-debug-results-20261017.json).
`install/imu-divergence/` contains all raw outputs, the same-camera OpenVINS
controls, state-window analysis, and `imu-debug.png`.

The standalone [`imu-diagnosis.patch`](imu-diagnosis.patch) targets local
SchurVINS commit `fefa113` and includes the earlier optional stability/pixel
helpers as well as the new diagnostics. Apply it by itself, rather than on
top of `estimator-stability.patch`. `SCHUR_IMU_DEBUG_CSV` enables the state
log; `SCHUR_IMU_MIDPOINT=1` enables only the integration experiment. Neither
adds online IMU calibration. The patch applies cleanly and the existing
estimator numerical/projection checks pass with the device YAML. Device
programs, saved calibration, and the main SchurVINS checkout remain unchanged.

```bash
install/dataset-test-venv/bin/python \
  scripts/vio-compare/probes/fpv-diagnosis/imu_model_probe.py \
  --binary install/fpv-stability/build/schurvins_stereo \
  --calibration install/fpv-stability/device-schurvins-20261017.yaml \
  --recording install/replay-20261012-084638/record \
  --intrinsics install/replay-20261012-084638/startup-diagnosis/imu-online-intrinsics/imu-intrinsics-printed.csv \
  --variants raw gyro-diagonal gyro-axis gyro-full accel-full \
  --output install/imu-divergence/reproduce-new
```

## Fixed offline calibration still diverges: lifecycle debugging (2026-10-17)

The two code defects below are now fixed in the local `SchurVINS` checkout.
The small, standalone change is also saved as
[`estimator-lifecycle-fix.patch`](estimator-lifecycle-fix.patch), against `fefa113`.
No device binary, device configuration, or recording was overwritten.
This updates the earlier sections' historical statement that the main checkout
was unchanged.

### Observation IDs and image IDs are different namespaces

`RegisterPoints()` inserts `LocalFeature` observations into a **multimap keyed
by augmented IMU state ID**. It correctly stores both stereo observations.
`Solve3()` previously passed the current **image bundle ID** to
`Point::CheckLocalStatus()`, which looked it up in that state-keyed multimap.
The previous-frame structure optimization fallback hid this error while old
tracks survived. New tracks could not reliably enter the solver after a loss.

In `084638`, the first skipped backend update produces bundle 229 versus state
228, at 11.476821 s after the first reported pose. In `003648`, failed visual
initialization leaves the first accepted bundle at 219 versus state 0;
the following update has bundle 220 versus state 1. The diagnostic control
has **zero nonempty Schur updates in all 186 backend calls** on `003648`.
The first post-initialization call already has two otherwise valid points
that the old lookup rejects and the state-ID lookup accepts.

On the first diagnostic build, ID correction alone changes `084638` from
418/884 nonempty updates to 881/884. With initialization reset as well,
`003648` changes from 0/186 to 535/574. These counts are actual solver
observations from `Solve3()`. The older `nObs` log reports surviving current
features and must not be interpreted as the number of solver observations.

### Failed visual initialization was already integrating a running state

`processFirstFrame()` calls IMU `Forward()` before attempting stereo
initialization. Previously, each failed attempt kept its propagated velocity,
position and covariance. With zero initial gyro bias, the 11 s visual startup
failure in `003648` accumulated an initial position near
`[20.575, 0.867, -10.838] m` and a speed of **5.971 m/s**, before the first
accepted visual clone. The old observation lookup then prevented correction.

`Forward()` now resets the state, IMU interval cursor and covariance when
there is no accepted clone and an earlier initialization attempt propagated a
state. It starts a fresh attempt from that frame's IMU interval. It does not
reset an initialized running estimator or repeatedly allocate a new state.

The fixed-intrinsic OpenVINS reference initializes its raw IMU state near
1.955 s, estimates gyro bias from a stationary window, and enables startup
zero-velocity updates. Its first 2 s gyro means agree with the reference
initial bias. Native SchurVINS previously had neither this startup behavior nor
the correct observation lookup. Offline scale/misalignment calibration cannot
repair these lifecycle errors.

### Matched ablations and remaining numerical sensitivity

All device results below use the **same fixed, offline equidistant calibration
with gyro acceleration sensitivity refitted to zero**, new camera timestamps,
and IMU correction from
`cali/tidesense-imucam-scale-misalignment-20261017-equi-nogs`.
Recorded six-face compensation remains applied exactly once. Online IMU
calibration is off. Original scalar IMU noise values remain fixed.

The table is from one diagnostic executable/library, with detailed CSV logging
**off**. Each entry was replayed twice with byte-identical pose CSVs. Neither
recording has external ground truth: these are **first-to-last distances, not
ATE**. All input frames were consumed (885 and 829); `003648` produces 610
poses after visual initialization.

| Variant | 084638 distance (m) | 003648 distance (m) |
|---|---:|---:|
| Control | 38.305629 | 792.056087 |
| Fixed point accumulation order only | 26.861827 | 792.056087 |
| State-ID lookup + failed-initialization reset | 3.368988 | 0.814090 |
| Also midpoint IMU integration + fixed point order | 1.115928 | 1.647177 |

The control of the earlier executable was 37.670114 m on `084638`.
Opening diagnostic files changes heap allocations; its default Schur point
set is ordered by `shared_ptr` address, changing the floating-point order of
Schur-complement accumulation. The address-order result is sensitive to this
change. The experimental `SCHUR_POINT_ID_ORDER=1` comparator uses stable point
IDs. With this option, **all four** comparisons across both recordings and
both tested variants have byte-identical trajectories with CSV logging on/off.
This identifies a reproducibility issue; ordering alone does not fix tracking.

On the corrected IMU input, the previous-sample integration control also shows
a benefit from midpoint integration. It remains experimental: this is a
separate numerical approximation, and it is not universally better.

Two UZH-FPV GT regressions, with matched IMU noise and full-frame coverage:

| Variant | indoor_forward_6 SE3 ATE RMSE (m) | outdoor_forward_1 SE3 ATE RMSE (m) |
|---|---:|---:|
| Control | 0.901652 | 1.243193 |
| State-ID lookup + initialization reset | 0.901652 | 1.243193 |
| Also midpoint + fixed point order | 1.073533 | 1.331470 |

The two lifecycle fixes have no effect on these runs where the problematic
startup/ID condition is not reached. Midpoint and point-order experiments are
**not enabled in the main checkout**. The existing estimator numerical and
camera projection tests also pass with the new experimental library.

A separate 2 s stationary IMU initializer was implemented and replayed as an
experiment. Without the ID fix it still fails; it does not reproduce OpenVINS'
full startup ZUPT/FEJ behavior. It is also kept out of the main checkout.

### Artifacts and reproduction

- [`root-cause-results-20261017.json`](root-cause-results-20261017.json):
  fingerprints, repeat checks, ablations, actual eligibility counters, GT
  regression and static-initialization controls.
- [`root-cause.patch`](root-cause.patch): optional diagnostic/ablation layer
  applied **after** `imu-diagnosis.patch` on a clean `fefa113` worktree. Its
  source reconstruction was checked byte-for-byte against the built source.
  This layer defaults all new experiment flags off.
- [`root_cause_ab.py`](root_cause_ab.py): full-record replay, per-run commands,
  exact input/library/executable hashes and pose repeat checks.
- `install/schur-cause/`: full logs and trajectories; `verified/` contains the
  repeated table runs, `verified-debug/` the logging controls, `public/` the GT
  regressions, and `root-cause.png` / `root-cause.pdf` the displacement plots.

```bash
install/dataset-test-venv/bin/python \
  scripts/vio-compare/probes/fpv-diagnosis/root_cause_ab.py \
  --binary install/schur-cause/build/schurvins_stereo \
  --validation-root install/kalibr-intrinsics-20261017 \
  --output install/schur-cause/reproduce-new \
  --variants baseline order id-reset id-reset-midpoint-order --repeats 2
```

For detailed counters add `--debug` and use a fresh output directory. Native
SchurVINS still consumes the residual-corrected IMU CSV in these experiments;
the main adapter has not gained a live Kalibr matrix loader from this fix.
The device's existing YAML and firmware remain untouched.

### Final verification of the small main-source fix

A fresh native build of the **actual modified main checkout**, without the
experimental solver, static initializer, midpoint or point-order code, was
also tested. An original-source control uses the identical executable and all
other object files, replacing only the SchurVINS translation unit with its
original `fefa113` source. The compile/link commands and `__FILE__` path mapping
are saved in `install/schur-cause/core-control/build-command.json`.
Each process explicitly loads the intended library; its SHA and inputs are
saved with the command.

| Actual main source | 084638 distance (m) | 003648 distance (m) |
|---|---:|---:|
| Original SchurVINS | 37.670114 | 792.056087 |
| Two lifecycle fixes, same fixed IMU calibration | 3.389314 | 1.099911 |
| Two lifecycle fixes, six-face-only IMU / same new cameras | 14.134248 | 17.175197 |

Each entry in this final device comparison has two identical pose replays.
The corresponding UZH original/control versus actual fixed-main builds give
**byte-identical trajectories**, SE3 ATE RMSE 0.737820 m on
`indoor_forward_6` and 1.371881 m on `outdoor_forward_1`, consuming all input
frames. These differ from the earlier experimental-build table because it has
a different heap layout and point accumulation ordering; its within-build
comparison remains valid.

`core-verified/`, `core-uncorrected/`, `core-control-verified/`, `core-public/`
and `core-control-public/` retain these final results. The frozen result JSON
contains all of them. Across the repeated final/diagnostic and IMU-control
runs there are 14 matching repeat pairs and four matching logging-toggle
comparisons. The standalone lifecycle patch was independently applied to
original `fefa113` files and reconstructed all three modified main-source
files byte-for-byte.

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

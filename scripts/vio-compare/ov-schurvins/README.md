# ov_SchurVINS host comparison

This adapter pins `Jas0nG/ov_SchurVINS` at
`d988139c0ba3cc51abacb8b5b5acb482a19325ba` and replays the same images,
IMU, calibration, and CSV trajectory format used by the local OpenVINS tests.
The fork retains OpenVINS tracking and initialization but replaces its MSCKF
update with `UpdaterSchurVINS`. No SDK runtime or TideSense image is changed.

The fork's Schur updater returns early if FEJ or camera pose/intrinsics online
calibration is enabled, and requires `GLOBAL_3D` MSCKF features. `run-host.sh`
copies each source configuration and sets `use_fej`, `calib_cam_extrinsics`,
and `calib_cam_intrinsics` to `false`; other settings remain as recorded. The
matching OpenVINS runs below used these exact copied configurations.

From the SDK root:

```sh
scripts/vio-compare/build.sh ov-schurvins-host
scripts/vio-compare/ov-schurvins/run-host.sh all
python3 scripts/vio-compare/ov-schurvins/evaluate.py
```

`run-host.sh` accepts `uzh`, `tum`, or `tidesense` to run one dataset. Its
outputs, source copies, build tree, and full logs are under
`install/vio-compare-20260924-121320/ov-schurvins/` and are Git ignored.
The TideSense case uses the existing dynamic initialization config and rotates
the right input by 180° as in the original OpenVINS replay. UZH-FPV uses
`indoor_forward_6`; TUM-VI uses `room1`; TideSense uses
`dataset-20260924-174756`. The checked-in CMake patch only adds two host replay
executables to the fork.

## Host results, 2026-09-26

| Dataset | Algorithm | Completed / initialized frames | Position ATE RMSE | First-to-last estimated displacement |
| --- | --- | ---: | ---: | ---: |
| UZH-FPV indoor_forward_6 | ov_SchurVINS | 1970 / 1782 | 77.93 m | 560.20 m |
| UZH-FPV indoor_forward_6 | OpenVINS, matching config | 1970 / 1782 | 0.455 m | 0.80 m |
| TUM-VI room1 | ov_SchurVINS | 2821 / 2690 | 1417.64 m | 5576.75 m |
| TUM-VI room1 | OpenVINS, matching config | 2821 / 2690 | 0.095 m | 1.04 m |
| TideSense dataset-20260924-174756 | ov_SchurVINS | failed at frame 303 / 259 initialized | no GT | 769.09 m before failure |
| TideSense dataset-20260924-174756 | OpenVINS, matching config | 1087 / 1044 | no GT | 5.03 m |

ATE is position-only, with an independent rigid SE(3) alignment and fixed
scale. UZH ground truth covers only 816 camera frames in a central 30.15 s
window. TUM ground truth covers 2690 initialized frames. The first-to-last
displacement is **not** drift or ground-truth error for the TideSense sequence.

The fork repeatedly logged `No features to update after triangulation!`. On
TideSense it exited in `StateHelper::SV_EKFUpdate` after detecting many negative
covariance diagonals. The fork's large UZH and TUM errors also occurred with
the same FEJ/calibration settings used by the matched OpenVINS controls. These
results establish that the fork's current Schur update is unstable on these
sequences; they do not establish whether a corrected Schur update could be
useful. Camera-call timings are preserved in `results/summary.json`, but this
experiment ran some processes concurrently and the diverged trajectories do
not make their timing a useful production performance comparison.

## Backend diagnosis, 2026-10-25

This experiment replays full EuRoC `V1_01_easy` (2912 stereo pairs), TUM-VI
`room1` (2821 pairs), and UZH-FPV `indoor_forward_6` (1970 pairs). It retains
the pinned source/binary above. Experimental changes live in a separate copy
under `install/ov-schurvins-validation-20261025/`; the SDK runtime is unchanged.

All methods use identical per-dataset configs: FEJ and camera pose/intrinsic
calibration are disabled as required by this Schur implementation. Time-offset
calibration, frontend, SLAM features, noise parameters, images, and IMU data
retain their existing settings. The replay forces one OpenCV thread and
disables subscription/publication worker threads. Some replays run concurrently;
wall times are not a performance comparison.

ATE fits a rigid rotation and translation with scale fixed to one. Evaluation
interpolates GT only within its coverage, applying the existing cam0 offset
of -16,684,572 ns for UZH association; no time offset is fitted. UZH GT covers
816 initialized frames in a central 30.15 s interval, rather than the whole run.

### Full replay results

Position ATE RMSE in metres:

| Backend | EuRoC V1_01_easy | TUM-VI room1 | UZH-FPV indoor_forward_6 |
|---|---:|---:|---:|
| Original ov-SchurVINS | covariance failure | 1417.643105 | 77.930408 |
| Unique-clone elimination only | 0.301537 | 0.144448 | 0.326706 |
| Unique clones + noise whitening | 0.057876 | 0.093243 | 0.449037 |
| Same-fork MSCKF backend | 0.057520 | 0.094570 | 0.454677 |
| Independent OpenVINS, matching config | 0.057520 | 0.094570 | 0.454677 |

All corrected/control cases complete every input frame. They initialize
2800/2690/1782 frames and evaluate the same 2780/2690/816 GT timestamps on
EuRoC/TUM/UZH respectively. Original EuRoC exits with 57 negative covariance
diagonal messages after the last progress report at frame 900. Only 866
complete CSV rows survive its unflushed output buffer, including 754 poses;
their partial ATE is 272.70 m and is not comparable with full-sequence ATE.
The diagnostic EuRoC binary also fails, with slightly different numerical
rounding/output buffering. Diagnostic TUM and UZH reproduce original ATE.

The combined correction repeats exactly on FPV: all 1970 timestamp,
initialization, and pose rows match, excluding compute timing. Both runs
produce 0.449037 m ATE. Noise correction alone gives 0.465953 m on FPV but
skips 859 indefinite Schur updates; retained SLAM updates still constrain the
state, so this is not evidence that the faulty elimination is usable.

The corrected Hessian can still contain cancellation noise around zero.
Whitening conservatively skips 25/14/3 such updates on EuRoC/TUM/UZH; it
does not suppress any output poses. No corrected run reports a negative
covariance diagonal. Remaining zero-triangulation messages fall from
718 to 87 on TUM and from 541 to 424 on UZH. Such a message alone is not
evidence of divergence: a batch can lack usable departing features while
other visual/SLAM constraints remain active.

The main divergence cause is repeated stereo elimination. The noise
correction restores agreement with MSCKF on the ordinary benchmarks; it
does not improve every dataset's ATE versus the elimination-only variant.
The lower FPV error of that variant does not justify keeping an inconsistent
noise model. These three sequences establish a reproducible repair candidate,
not general robustness across the full dataset suites or on the A53 device.

Raw results: [summary](../../../install/ov-schurvins-validation-20261025/summary.json),
[position-error plot](../../../install/ov-schurvins-validation-20261025/position-errors.png),
and [binary/configuration provenance](../../../install/ov-schurvins-validation-20261025/provenance.json).

### Host backend timing, 2026-10-25

A separate sequential experiment compares the corrected Schur update with
the same executable's MSCKF backend. It enables the existing stage timer,
uses one OpenCV thread, and disables diagnostic matrix/Jacobian export.
Schur eigenvalue whitening remains enabled as part of the repair. Dataset
configs match the accuracy experiment, apart from enabling timing and using
separate timing output paths. Every replay completes and reproduces its
untimed trajectory exactly, excluding compute timing.

Means in milliseconds per initialized camera frame:

| Dataset | Runs per backend | Schur backend | MSCKF backend | Schur visual-update phase | MSCKF visual-update phase |
|---|---:|---:|---:|---:|---:|
| EuRoC V1_01_easy | 1 | 6.684 | 7.095 | 0.720 | 0.985 |
| UZH-FPV indoor_forward_6 | 2 | 3.418 | 3.323 | 0.346 | 0.323 |

Backend time sums propagation, MSCKF/Schur update, SLAM update, delayed SLAM
initialization, and re-triangulation/cleanup/marginalization. Tracking, image
loading, and stage-file output are outside this sum. The visual-update phase
also includes shared feature selection and housekeeping around the updater.
Samples cover 2800 initialized frames per EuRoC run and 1782 per UZH run.
Repeated UZH means are 3.386/3.451 ms for Schur and 3.402/3.245 ms for MSCKF.

Schur's backend mean is about 6% lower in this EuRoC run and about 3% higher
on UZH, within the observed host timing variability. This supports comparable
backend cost for these configurations, rather than a general speed ranking.
Only the MSCKF visual updater changes; propagation, SLAM, covariance
management, and marginalization still use the OpenVINS implementation.
These are local x86 measurements, without CPU affinity or frequency controls;
they do not establish A53 performance or compare the native SVO-based
SchurVINS backend.

The committed [timing record](backend-timing-20261025.json) preserves all six
run summaries, execution order, commands, environment switches, timing
definitions, trajectory checks, and executable/library fingerprints. Full
logs, configs, per-frame stage timings, and trajectories remain under
`install/ov-schurvins-validation-20261025/backend-timing/`.

### Confirmed elimination error

`UpdaterSchurVINS.cpp` sums `Jx^T Jf` across cameras into
`feature.stack_W_map[clone_id]`. Its elimination loop then revisits that
already-summed entry once per camera observation. When both cameras see one
landmark at the same clone, it subtracts the combined Schur term repeatedly.
The gradient has the same problem. Unequal camera visibility can also omit
cross-camera clone pairs because the elimination loops are camera-local.

The correct contribution is `H = Jx^T Jx - W V^-1 W^T` and
`b = Jx^T r - W V^-1 Jf^T r`, where `V = Jf^T Jf` and each row block of `W`
already combines all cameras at one clone. Elimination must visit unique
clone pairs independently of which camera supplied each observation.

The patch exports real weighted residuals/Jacobians from the first update.
`diagnose.py` independently projects them into the left nullspace of `Jf`
with SVD and compares that result against both Schur loops:

| First update | Upstream minimum eigenvalue | Upstream relative Hessian error | Corrected relative Hessian error |
|---|---:|---:|---:|
| EuRoC | -54005.08 | 0.98694 | 2.55e-14 |
| TUM-VI | -13779.40 | 0.53456 | 5.91e-14 |
| UZH-FPV | -45783.82 | 0.44722 | 1.92e-15 |

This error is present before trajectory divergence or loss of triangulation.
A valid eliminated least-squares Hessian is positive semidefinite; these
negative eigenvalues are not gauge freedom or roundoff. Corrected minimum
eigenvalues are approximately -1e-9 to -1e-10 against maximum eigenvalues of
order 1e4 to 1e5.

### Inconsistent normal-equation noise

The upstream call passes information matrix `H` and gradient `b` as though
they were a measurement Jacobian and residual, with noise `sigma^2 I`.
The corresponding normal-equation noise is `sigma^2 H`. Using identity
changes the information weighting, effectively introducing a squared-Hessian
model. For an independent seeded positive-definite prior and unit noise, the
real first-update Jacobians give a 12.7%, 25.2%, and 148.6% difference from
the correct state increment on EuRoC, TUM, and UZH respectively; whitening
agrees with the information-form reference to about 1e-13 relative error.

The experimental correction diagonalizes the symmetric Hessian and retains
eigenvalues above `max(1e-10, lambda_max * 1e-10)`. It uses
`J = sqrt(Lambda) Q^T / sigma` and `r = Lambda^-1/2 Q^T b / sigma` with unit
covariance, avoiding direct inversion of gauge directions. Substantially
indefinite matrices are rejected. The existing EKF helper applies the update.

The experiment preserves upstream `obs_invdev=0.25` and Huber weighting.
It corrects the compressed model without establishing calibrated pixel noise.
FEJ support, Schur chi-squared gating, and the hardcoded observation weight
remain separate limitations.

### Reproduction

`diagnostic.patch` applies to the pinned fork after its existing host adapter
patch. `prepare` copies the source and replay wrappers before applying it.
The existing host OpenCV/Ceres dependencies and OpenVINS control executable
are prerequisites, as for the original comparison.

```bash
python3 scripts/vio-compare/ov-schurvins/diagnose.py prepare \
  --euroc-archive ~/fpga_vio/datasets/source/V1_01_easy.zip
cmake -S install/ov-schurvins-validation-20261025/source/ov_msckf \
  -B install/ov-schurvins-validation-20261025/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-11 \
  -DENABLE_ROS=OFF -DENABLE_ARUCO_TAGS=OFF \
  -DOpenCV_DIR="$PWD/install/dataset-analysis-20260924-155813/pc/stage/share/OpenCV" \
  -DCeres_DIR="$HOME/fpga_vio/.tools/ceres-1.14/lib/cmake/Ceres" \
  -DEigen3_DIR=/usr/share/eigen3/cmake
cmake --build install/ov-schurvins-validation-20261025/build --target run_record -j2
python3 scripts/vio-compare/ov-schurvins/diagnose.py run \
  --variants original openvins diagnostic stack fixed msckf --jobs 2
python3 scripts/vio-compare/ov-schurvins/diagnose.py run \
  --variants noise fixed-repeat --datasets uzh
python3 scripts/vio-compare/ov-schurvins/diagnose.py summarize
PYTHONPATH=/usr/lib/python3/dist-packages MPLCONFIGDIR=/tmp/ov-schurvins-mpl \
  /usr/bin/python3 -S scripts/vio-compare/ov-schurvins/diagnose.py plot
```

Variants: `diagnostic` retains upstream math and exports matrices; `stack`
fixes unique-clone elimination only; `noise` fixes compressed noise only and
rejects indefinite updates; `fixed` combines both corrections; `msckf`
switches only the backend call to this same fork's existing `UpdaterMSCKF`.
`openvins` uses the independent existing executable with the identical copied
config. `fixed-repeat` repeats the combined fix.

Each case retains `run.log`, `trajectory.csv`, and `evaluation.json`.
Diagnostic cases also retain `information.csv` and `first-linearization.csv`.
`summary.json` contains trajectory metrics and independent matrix checks;
`provenance.json` records source/configuration and binary hashes. To use a
different output tree, pass the same `--output` to each Python invocation
and update both CMake paths accordingly.

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

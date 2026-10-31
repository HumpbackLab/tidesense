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

后端诊断使用 `diagnose.py prepare|run|summarize|plot`；
独立构建、修正项和矩阵检查见验证记录，不替换默认运行时。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

# TideSense OpenVINS IMU-guided LK and covariance timing probe

This is an experimental, opt-in A53 replay build. `openvins-core.patch` adds
camera-frame rotation guesses to stereo temporal LK and timing around
`StateHelper` covariance operations. `run_record.patch` integrates gyro
measurements between camera timestamps, subtracts the current gyro bias,
converts each rotation with that camera's `R_ItoC`, and passes it to the
tracker. The tracker undistorts each old point, rotates the ray, and distorts
it back to the current image. The baseline uses the same binary without
`OPENVINS_IMU_LK`; `OPENVINS_LK_PROBE` and `OPENVINS_COV_PROBE` only enable
timing logs. Original OpenVINS settings are otherwise unchanged.

These patches apply to the checkout created by `scripts/vio-compare/sync.sh`:

```sh
cd install/vio-compare-20260924-121320/openvins/open_vins
git apply ../../../../scripts/vio-compare/probes/imu-lk-cov/openvins-core.patch
cd ..
patch -p1 < ../../../scripts/vio-compare/probes/imu-lk-cov/run_record.patch
```

The relative paths above assume the default work directory. Build with
`cmake --build build-a53 --target run_record -j4`, strip the result for the
board, and set the environment variables on the replay command. The probe
currently lives in the ignored work checkout and is not part of the normal
OpenVINS build path.

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

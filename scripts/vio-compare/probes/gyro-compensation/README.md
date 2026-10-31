# OpenVINS gyro frontend compensation experiment

This probe compares the existing frontend, gyro rotation as LK initial flow,
and rotation of the previous raw image into the current camera orientation.
It uses the calibrated camera model, including equidistant fisheye distortion.
Only temporal LK consumes the rotated coordinates. RANSAC and the feature
database retain the original previous-image coordinates.

The standalone adapter integrates midpoint gyro measurements between camera
acquisition times, subtracts the current gyro bias and uses each camera's
`R_ItoC`. Camera/IMU time offset is applied once. Integration, projection,
image remapping and pyramid construction are included in camera processing
time. There is no IMU noise scaling, backend precision change, deblurring,
exposure adjustment or dropped camera frame.

## Switches and reproduction

`OPENVINS_GYRO_FRONTEND` defaults to **0**. The isolated experiment build
defines it as 1; `OPENVINS_GYRO_MODE` then selects `off` (default), `initial`
or `warp`. Both compensation modes require rotations supplied by the replay
adapter. This experimental interface supports one VIO instance and one OpenCV
thread; it is not wired into the live adapter or the standard SDK build.

The verified host FP64 diagnostic source/archive and the two local PGM exports
must already exist under the paths used by the build and run scripts.
The output work directory must not exist:

```sh
python3 scripts/vio-compare/probes/gyro-compensation/build.py \
  --work install/openvins-gyro-compensation
python3 scripts/vio-compare/probes/gyro-compensation/run_experiment.py \
  --work install/openvins-gyro-compensation
```

The builder copies the source into the work directory and replaces only the
tracker and standalone replay objects in a copy of the FP64 archive. Generated
`openvins.patch` and `run_record.patch` are retained there for inspection.
The runner feeds every frame sequentially with ROS disabled, 150 point budget,
16x15 LK window, failed-LK filtering enabled and the new equidistant stereo and
cam–IMU calibration. Other estimator settings remain identical across modes.

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

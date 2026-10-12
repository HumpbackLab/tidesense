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

## 2026-10-12 results

Full replay of `dataset-20261012-003648`: 829 stereo pairs, 8307 IMU samples,
41.5 seconds. Actual initial/final positions were reported within 1 m.

| Mode | Final displacement | Mean camera processing | P95 camera processing |
| --- | ---: | ---: | ---: |
| Off | 189.39 m | 7.05 ms | 21.90 ms |
| Gyro initial flow | 72.71 m | 4.72 ms | 11.04 ms |
| Previous-image rotation | 157.54 m | 36.01 ms | 48.18 ms |

Timings are from the **PC**, on the same 619 initialized output timestamps.
They include diagnostics and are single-run measurements. Reduced backend
work after rejection can reduce processing time while the trajectory remains
invalid; the initial-flow timing is not an accuracy-preserving optimization.

At 17–19 seconds, median retained left/right/shared tracks were 8/9/2 without
compensation, 11.5/14/3 with initial flow, and 12/13/3 with image rotation.
Frames with no accepted MSCKF/SLAM/delayed-initialization visual update remained
60%, 62.5% and 62.5%, respectively. More tracks did not restore enough reliable
visual constraints to prevent divergence.

Regression replay of `dataset-20261011-213326`, using the same new calibration:
final displacement was 0.528 m / 0.593 m / 0.492 m for off / initial / warp.
Warp held startup zero-velocity updates longer, so its first published pose
was at 21.049 s rather than 6.114 s in the off run. These endpoint values are
not an absolute trajectory accuracy comparison; neither recording has ground
truth. Both off runs reproduced the previous position/quaternion outputs
exactly.

An independent affine image rotation around the calibrated optical axis
provided a clean-image geometry check. At 70 degrees, image compensation
tracked 92 of 150 detected points with median error 0.0016 px against the
analytic pixel mapping. Initial flow alone returned only 22 successful LK
statuses, with median error 66 px before RANSAC. This validates the basic
rotation/remapping coordinate handling; it does not simulate the real
recording's blur, changing field of view or translational parallax.

Raw sources, patches, logs, trajectories, summaries and the synthetic check:
`install/replay-20261012-003648/gyro-compensation/`.

**Keep compensation disabled by default.** Neither variant resolved the
aggressive replay, and the full-image warp has substantial additional cost.
Remaining causes cannot be assigned solely to exposure from this experiment.

# 20 Hz visual correction / 200 Hz IMU prediction validation

2026-09-24, TideSense SG2002 A53, GCC 11 static build, existing NEON options ON.

## Checks

- Native standalone and A53 micro-ROS builds pass.
- `schurvins_prediction_test` passes on PC and the board: stationary gravity
  cancellation, rotation, acceleration, bias, delayed/fractional corrections,
  monotonically increasing timestamps, gap/age invalidation and worker drain.
- Native replay of `dataset-20260924-174756`: 1,087 visual poses and 10,861
  predicted poses. Prediction interval min/median/max 4.935/5.012/5.086 ms,
  average 199.537 Hz, all finite and strictly increasing.
- Live board test: 400 processed stereo frames, 382 tracking poses after startup;
  4,043 IMU samples, no discarded IMU, maximum IMU gap 5.062 ms. Camera input
  19.954 Hz; two pending image pairs were replaced. Visual correction therefore
  follows processed camera frames and is not guaranteed to sustain exactly 20 Hz.
- Board produced/published 3,833 predicted poses, ROS 2 Cyclone subscriber received
  all 3,833. Timestamp frequency 199.539 Hz; wall-clock receive frequency 199.582 Hz.
  No publisher queue drops. All received poses match prediction CSV to <6e-16;
  ROS minus monotonic timestamp is constant 1790220250490792880 ns for this run.
- Agent used UDP 18894/domain 29 and the existing `calibration-agent.xml` loopback
  DDS profile, subscriber used `ros2-env.sh`. Omitting the Agent profile in the
  first attempt resulted in no subscriber delivery despite successful publishing.
  Follow the README's Agent setup on this host.

These are output/integration checks, not a VIO accuracy or hard real-time guarantee.
The violent recording still diverges. Enabling/disabling prediction also did not
produce bitwise-identical visual trajectories: differences begin near floating
point precision and become large later in this already unstable recording.
Pinned repeated OFF runs were identical. No prediction state is written back to
the estimator. The backend's `std::set<PointPtr> schur_pts_` determines matrix
accumulation order from pointer ordering; new allocations can change that order.
This is a plausible numerical sensitivity source, not a proven diagnosis of every
trajectory difference. No estimator numerical-order change is included here.

Raw logs, CSVs, receiver script and result.json are under the SDK ignored directory
`install/prediction-validation/`. The board's previous executable is retained as
`bin/schurvins_stereo.before-prediction-20260924` in the micro-ROS deployment.

Build the focused test with:

```sh
cmake --build BUILD_DIR --target schurvins_prediction_test
BUILD_DIR/schurvins_prediction_test
```

For the A53 binary, copy it to the board and run there.

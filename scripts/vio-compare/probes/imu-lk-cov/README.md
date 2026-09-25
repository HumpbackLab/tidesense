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

## IMU prediction A/B test on TideSense A53

Both runs used the same GCC 11 static binary and one OpenCV thread. Camera
images were 640x400 at 20 Hz; IMU was 200 Hz. Timings below exclude file
reading and image rotation. `LK` includes per-point ray projection; the
separate gyro integration took 0.043 ms/frame on the strong-motion replay.

| Dataset and initialized frames | Baseline LK | IMU LK | Baseline good L+R | IMU good L+R | Full camera update |
| --- | ---: | ---: | ---: | ---: | ---: |
| `vio-record-20260924-121320`, 417 | 23.86 ms | 23.54 ms | not logged | not logged | 75.29 / 76.28 ms |
| `dataset-20260924-174756`, 1044 | 67.89 ms | 50.34 ms | 108.0 | 112.8 | 97.77 / 79.03 ms |

The strong-motion replay used `openvins-dynamic-config` from its local
analysis, except `init_dyn_mle_max_iter` was set to zero for **both** runs.
The original 50 ms dynamic-MLE time cap prevents initialization on this A53:
after 200 frames the baseline still had no initialized state and took
hundreds of milliseconds per frame. Skipping the optional MLE refinement
initialized on frame 43, allowing a paired tracker comparison. It changes
the estimator initialization, so these trajectories are not an accuracy
benchmark for the original configuration.

The LK benefit is concentrated at high rotation rates. The bins below use
the mean gyro magnitude within 25 ms of each camera timestamp, pairing the
same frames in both runs:

| Gyro magnitude | Frames | LK baseline / IMU | Good L+R baseline / IMU |
| --- | ---: | ---: | ---: |
| 0–1 rad/s | 77 | 18.2 / 18.3 ms | 118.1 / 117.2 |
| 1–3 rad/s | 514 | 32.6 / 32.4 ms | 128.2 / 129.4 |
| 3–5 rad/s | 302 | 72.3 / 51.4 ms | 96.7 / 101.6 |
| 5–8 rad/s | 171 | 155.0 / 92.3 ms | 59.9 / 74.9 |
| >8 rad/s | 19 | 242.7 / 184.5 ms | 32.4 / 52.5 |

The prediction helps LK converge and retain more tracks during strong
rotation, but even 92 ms LK at 5–8 rad/s is far beyond a 50 ms frame period.
Both full VIO trajectories on this aggressive dataset diverged severely;
their final positions differ by several kilometres. The probe therefore
supports an LK tracking and CPU-time benefit, **not** a VIO accuracy claim.
The milder dataset shows essentially no performance benefit.

Raw local logs and trajectories are under `install/openvins-pipeline-20260925/`.

## Covariance update timing

A separate 550-frame replay of `vio-record-20260924-121320` had 223
initialized frames. Across these frames, 711 `StateHelper::EKFUpdate` calls
used 15.64 ms/frame combined. The segments are nested within the previously
measured 34.5 ms/frame backend; do not add them to its total.

| EKF update segment | Mean per initialized frame | Share of measured EKF time |
| --- | ---: | ---: |
| Assemble `M_a = P H^T` in variable/block loops | 7.59 ms | 49% |
| Extract small covariance | 0.53 ms | 3% |
| Form/solve residual covariance and gain | 3.63 ms | 23% |
| Update and mirror full covariance | 3.29 ms | 21% |
| Check diagonal and update variables | 0.60 ms | 4% |

There were 342 accepted and 454 rejected delayed landmark-initialization
attempts. Rejected attempts spent 2.91 ms/frame in their gating path.
Accepted attempts spent 9.82 ms/frame in `StateHelper::initialize` including
the nested EKF update. The covariance augmentation itself cost 1.78 ms/frame;
its resize/copy portion was only 0.43 ms/frame. Repeated covariance growth
is therefore a lower priority than `M_a` assembly.

`StateHelper::EKFUpdate` walks every state variable and every measured
variable, performing many tiny products into `M_a`. A grouped/packed
`P[:, H_order] * H^T` calculation, with equivalent ordering and no extra
full-matrix copy, is the first backend candidate to benchmark. Even a
twofold speedup of this segment saves at most about 3.8 ms/frame on this
recording. Follow it with the residual-covariance solve and covariance
writeback only after checking numerical equivalence and replay trajectories.

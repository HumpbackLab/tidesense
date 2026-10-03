# OpenVINS EKF mixed-precision A53 experiment

`StateHelper::EKFUpdate` spends substantial time forming `M_a = P Hᵀ` from
many small `double` matrix products. This probe packs the measured covariance
columns once and supports three modes:

| `OPENVINS_EKF_MA_MODE` | Computation |
| --- | --- |
| `original` | Upstream block-by-block `double` products |
| `packed_double` | Packed covariance and one `double` product |
| `packed_float` | Packed covariance and one `float` product; cast result back to `double` |

With `OPENVINS_EKF_MA_EXPERIMENT=ON`, `packed_float` is the default. Without
the build option, the original source path is compiled. State variables, the
stored covariance, residual covariance solve, Kalman gain, and covariance
writeback stay in `double`. `OPENVINS_EKF_MA_PROBE=1` logs each product's
matrix sizes and elapsed milliseconds. The A53 object contains `fmla v*.4s`
instructions in this path, confirming NEON FP32 vectorization.

## Build and run

From the SDK root, on an OpenVINS source checkout with the maintained offline
patches and without unrelated TPU modifications:

```bash
OPENVINS_EKF_MA_EXPERIMENT=1 scripts/vio-compare/build.sh openvins
```

`sync.sh` applies `openvins-core.patch` and `openvins-cmake.patch` only when
that build option is requested. The original mode remains available without
rebuilding:

```bash
OPENVINS_EKF_MA_MODE=original ./run_record config.yaml record_dir original.csv 0
OPENVINS_EKF_MA_MODE=packed_double ./run_record config.yaml record_dir double.csv 0
OPENVINS_EKF_MA_MODE=packed_float ./run_record config.yaml record_dir float.csv 0
```

The board experiment used an isolated OpenVINS checkout at commit `6948812`
with `openvins-offline.patch`, `openvins-headless.patch`, and this probe's two
patches. It was compiled using GCC 11.3, `-mcpu=cortex-a53`, Eigen 3.4.0,
OpenCV 3.4.20, and a static A53 executable. The three modes used the **same
binary**, configuration and `vio-record-20260924-121320` recording. The
recording has no ground truth, so the pose comparison below is numerical
agreement with the original run, not VIO accuracy.

## A53 measurements

550-frame replay: 223 initialized frames and 711 EKF updates in every mode.
Mean camera time includes frames before initialization and excludes image
decode and log/CSV writing.

| Mode | `M_a` sum | `M_a` mean/update | Camera mean/frame | Final position difference vs original |
| --- | ---: | ---: | ---: | ---: |
| Original | 1640.9 ms | 2.308 ms | 41.47 ms | — |
| Packed double | 655.1 ms | 0.921 ms | 39.79 ms | 0.00000008 m |
| Packed float | 362.0 ms | 0.509 ms | 38.95 ms | 0.00000694 m |

The packed `double` path cuts the `M_a` time by 60%; the packed `float` path
cuts it by 78% versus original, and by 45% versus packed `double`. Overall
camera time improves by 4.1% and 6.1%, respectively. This shows the gain
comes partly from eliminating repeated small products and partly from FP32
vector arithmetic.

Full 744-frame replay: 417 initialized frames and 1367 EKF updates in both
original and packed-float modes.

| Mode | `M_a` sum | Camera mean/frame | Final position difference | Final attitude difference |
| --- | ---: | ---: | ---: | ---: |
| Original | 3738.3 ms | 51.15 ms | — | — |
| Packed float | 779.1 ms | 47.30 ms | 0.0000120 m | 0.000298° |

The full replay improves camera time by 7.5%. Its initialized-frame pose
differences have median 0.00000985 m and p95 0.0000152 m. The results support
this **opt-in** mixed-precision step for the tested board recording. More
recordings, especially those with strong rotation and different calibration,
are needed before making FP32 arithmetic unconditional across the backend.
The subsequent local `indoor_forward_6` UZH-FPV test completed all 1970 frames:
mean camera time was 6.60 ms original versus 5.72 ms packed-float, and backend
time was 2.22 versus 1.52 ms. GT ATE RMSE was 0.238 versus 0.223 m. However,
the final position differed from FP64 by 0.218 m, so the tiny pose difference
on the ordinary recording does not generalize to this sequence. See the
[backend probe report](../backend-f32/README.md) for the paired test and
covariance diagnostics.
After rebuilding the final patch, a 350-frame board smoke run with no mode
variable logged `EKF_MA packed_float` and initialized 23 frames.

Raw replay results are in the ignored
`install/openvins-ekf-f32/results/` (550 frames) and
`install/openvins-ekf-f32/results-744/` (full recording) directories.
`run_a53.sh` and `analyze.py` reproduce the three-mode comparison.

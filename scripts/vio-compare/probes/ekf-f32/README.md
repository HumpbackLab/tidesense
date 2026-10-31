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

局部 FP32 为显式实验；当前常规测试与实时程序使用 FP64。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

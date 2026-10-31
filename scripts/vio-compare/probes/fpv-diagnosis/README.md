# SchurVINS FPV 诊断

用于本机回放 UZH-FPV 和 TideSense 数据，检查视觉观测、IMU 积分和后端数值行为。
当前源码的主机构建及检查：

```sh
scripts/vio-compare/probes/fpv-diagnosis/build_host.sh SchurVINS install/schur-host
install/schur-host/schurvins_imu_intrinsics_test
install/schur-host/schurvins_stereo_registration_test
install/schur-host/schurvins_midpoint_pixel_test scripts/schurvins/synthetic.yaml
```

`build_host.sh SOURCE BUILD_DIR` 使用已有主机依赖；运行检查需对应核心接口存在。
TideSense 实时运行和普通回放见 [SchurVINS](../../../schurvins/README.md)。

## 诊断入口

- `instrumentation.patch`：旧版 `afd180b` 的前后端 CSV 和视觉噪声对照，仅用于独立工作树。
- `084638_followup.py`：084638 数据的受控对照，需已有回放适配器、输入及依赖。
- `084638.env`：中点积分与像素残差实验配置。
- `stereo-registration.patch`：双目关键帧观测补注册的历史核心差异。

既有 084638 工作区的四项对照：

```sh
install/dataset-test-venv/bin/python \
  scripts/vio-compare/probes/fpv-diagnosis/084638_followup.py \
  --library install/schur084638/retain-two-build \
  --output install/schur084638/reproduce-retained \
  --variants baseline midpoint pixel candidate
```

历史补丁、噪声参数和临时开关不能直接作为当前部署配置。
完整排查、各数据集结果、输入前提和复现命令见 [VALIDATION.md](VALIDATION.md)。

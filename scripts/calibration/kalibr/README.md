# Kalibr 一键工具

在工程根目录运行，使用 rootless Podman / ROS Noetic，无需主机安装 ROS 1。
先安装 Podman、catatonit；不要用 sudo 运行脚本。录制与摆板见 [标定流程](../README.md)。
支持录制器导出的 ROS 1 bag 文件，也支持已有 ROS 2 bag 目录。

## 构建

```sh
scripts/calibration/kalibr/build.sh
# 仅更新运行依赖，不重编 C++
scripts/calibration/kalibr/build.sh --runtime-only
```

镜像 `localhost/tidesense-kalibr:noetic`，Kalibr 锁定版本见构建脚本，
缓存位于 `install/kalibr-podman/`；可用 `KALIBR_BUILD_JOBS` 控制作业数。
运行标定时禁用容器网络，输入只读，结果归当前用户；无需 X11 转发。

## 双目标定

```sh
scripts/calibration/kalibr.sh cali/tidesense-stereo-calib
scripts/calibration/kalibr.sh /path/to/ros2-bag \
  --lens fisheye --target /path/to/target.yaml \
  --topics /left/image_raw /right/image_raw --output cali/cameras
```

默认 A4 6×4 / 32 mm AprilGrid，`--lens wide` 对应两路 pinhole-radtan，
`fisheye` 对应 pinhole-equi。ROS 2 默认图像话题为 `/tidesense/left/image_raw`、
`/tidesense/right/image_raw`；通用录制器的 ROS 1 bag 使用 `/cam0/image_raw`、`/cam1/image_raw`。
图像不旋转、不裁剪、不去畸变，只保留时间戳完全一致的双目对。

结果默认在输入同级的 `输入名-kalibr-时间/`，拒绝覆盖。输出 `stereo.bag`、
`stereo.conversion.json`、`stereo-camchain.yaml`、结果 TXT 和报告 PDF，附输入副本与日志。
检查覆盖和残差，不应仅看平均误差或设计基线。仅检查/转包可加 `--convert-only`。

## 相机–IMU 联合标定

```sh
scripts/calibration/kalibr.sh cali/tidesense-imucam \
  --imu-cam --cam cali/cameras/stereo-camchain.yaml \
  --imu cali/sc7u22-imu.yaml --output cali/imucam
```

IMU YAML 需真实采样率、话题和四个噪声参数，不接受 null 模板。
相机话题/模型/尺寸从 camchain 读取，不再指定 `--topics` / `--lens`。
双目与联合阶段保持同一图像方向和尺寸，包括倒装的右相机。

固定使用 `scale-misalignment`，同时估计时间偏移；默认固定陀螺加速度敏感矩阵 A 为零，
显式加 `--estimate-g-sensitivity` 可对照估计。不支持 `calibrated` 或 size-effect 模型。
输入须为标称原始 SI，不能重复应用历史内参补偿；运行时统一应用逆模型，残余零偏由 VIO 估计。

输出 `imucam.bag`、转换摘要、`imucam-camchain-imucam.yaml`、结果 TXT/PDF，
并自动导出 `schurvins.yaml`。已有结果单独转换：

```sh
scripts/calibration/kalibr.sh --export-schurvins cali/imucam
```

默认文件已存在时拒绝覆盖，可用 `--output /path/to/new.yaml` 指定新文件。

## 静止 IMU Allan 分析

```sh
scripts/calibration/kalibr.sh cali/sc7u22-static --allan
scripts/calibration/kalibr.sh cali/static.bag --allan --imu-topic /imu0
```

可加 `--skip-seconds`、`--duration`、`--output`。不需要图像或重建容器。
输出六轴曲线 PNG/PDF/CSV、拟合 JSON、报告及 Kalibr `imu.yaml`。
拟合证据不足的分量写 null，三轴汇总取最大值；正式求解前检查数据长度、静止性与拟合区间。

## 已有转包目录重拟合

目录需包含原始 SI 的 `imucam.bag`、`camchain-input.yaml`、`imu-input.yaml`、`target.yaml`：

```sh
python3 scripts/calibration/kalibr/calibrate_intrinsics.py \
  --source cali/tidesense-imucam-converted \
  --output cali/tidesense-imucam-intrinsics --disable-g-sensitivity
scripts/calibration/kalibr.sh --export-schurvins cali/tidesense-imucam-intrinsics
```

转换保留数值及采集时间，以 header 时间重建 bag 索引；不修复传感器同步。
时间倒退、非有限 IMU、缺失话题、图像规格不符、双目少于 20 对会报错。
20 对仅是输入下限。构建实测、回归命令和模型公式见 [VALIDATION.md](VALIDATION.md)。

# Podman ROS 1 / Kalibr 环境

本机端到端结果见 [验证记录](VALIDATION.md)。

在 SDK 根目录操作。运行时使用 rootless Podman，不需要主机安装 ROS 1，
也不需要在 ROS 2 终端 source ROS 1。容器不访问相机或网络，不连接正在运行的 Agent。
运行使用 rootless 默认 UID 映射，容器内 root 对应宿主机当前用户；不要用 sudo 执行脚本。
Podman 的 `--init` 用于保证 Xvfb 就绪信号和子进程回收正常（需安装 catatonit，本机已有）。

## 构建一次

当前机器已构建完成，可直接使用下一节的标定命令。以下仅用于首次搭建或重建：

```bash
scripts/calibration/kalibr/build.sh
```

镜像：`localhost/tidesense-kalibr:noetic`，基础环境 Ubuntu 20.04 / ROS Noetic，使用 AWS ECR 的 Docker 官方 ROS 镜像副本。
Kalibr 固定为上游 `1f60227442d25e36365ef5f72cd80b9666d73467`，
依赖依据 [上游 Containerfile](https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/Dockerfile_ros1_20_04)。
转换使用 `rosbags==0.9.23`，构建缓存位于 `install/kalibr-podman/`。
Ubuntu 软件包默认使用中科大镜像，Python 包使用清华镜像，ROS 使用 Noetic 官方最终快照。
默认同时编译 2 个作业；可设置 `KALIBR_BUILD_JOBS`。本机 16 GB 内存使用 6 个作业编译通过，
纯编译约 19 分钟，首次下载和安装依赖另计。构建使用 host 网络，
以便访问本机已有的 HTTP 代理；运行标定时禁用容器网络。

## 一条命令：ROS 2 bag → 双目标定

```bash
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-stereo-calib
```

默认在 bag 同级创建 `tidesense-stereo-calib-kalibr-时间/`，原始 bag 只读挂载。
结果目录已存在时拒绝覆盖。指定输出目录：

```bash
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-stereo-calib \
  --output ~/sophpi/cali/stereo-calibration-result
```

默认：左话题 `/tidesense/left/image_raw`、右话题 `/tidesense/right/image_raw`，
两路模型 `pinhole-radtan`，标定板为本 SDK A4 6×4 / 32 mm AprilGrid。
如果实际镜头适合其他模型，可以显式指定，例如：

```bash
scripts/calibration/kalibr.sh /path/to/ros2-bag \
  --models pinhole-equi pinhole-equi \
  --target /path/to/target.yaml \
  --topics /left/image_raw /right/image_raw
```

这是原始 `mono8` 图像的双目标定入口，不旋转、镜像、裁剪或去畸变图像。
左图映射到 cam0，右图映射到 cam1；倒装是否在采集端校正必须与正式运行保持一致。
镜头模型应根据镜头和误差分布选择，不应只看优化后的平均误差。

## 输出和检查

### 相机–IMU 模式

已完成双目标定后，对包含双目图像和 IMU 的 ROS 2 bag 执行：

```bash
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-imucam \
  --imu-cam \
  --cam /path/to/stereo-camchain.yaml \
  --imu /path/to/sc7u22-imu.yaml
```

`--cam` 使用双目标定生成的 YAML；`--imu` 需要真实的更新频率、话题和四个噪声参数，
不能直接使用含 null 的模板。相机话题和模型从 camchain 读取，因此不要再传
`--topics` / `--models`。保持双目标定时的分辨率与图像方向，包括右相机倒装 180°。
无需先在板端加载双目内参、去畸变或旋转图像。

脚本自动转换并调用 `kalibr_calibrate_imu_camera`，使用 `calibrated` IMU 模型，
默认同时估计时间偏移。不自动校正输入 IMU 的尺度、零偏或单位。
所有 IMU 消息均保留，用各自 header 时间作为 bag 索引时间；图像仍仅保留精确配对帧。
检查 IMU 时间戳递增、有限数值、与图像时间重叠以及图像分辨率是否匹配 camchain。
转换摘要附带 IMU 数量、平均频率、最大时间间隔和与图像重叠的样本数。

输出为 `imucam.bag`、`imucam.conversion.json`、`imucam-camchain-imucam.yaml`、
`imucam-results-imucam.txt` 和 `imucam-report-imucam.pdf`。
也支持 `--target`、`--output` 和 `--convert-only`。原始输入只读，结果目录拒绝覆盖。
相机/IMU YAML、转换脚本副本、容器信息和日志一并保存。
入口使用当前仓库的转换脚本。旧镜像的 Focal SciPy 1.3 与 NumPy 1.24 不兼容，
相机–IMU 模块会报 `numpy.typeDict` 不存在。已将 SciPy 固定为 1.10.1。
本机已升级；其他机器可只更新运行依赖，无需重编译 Kalibr C++：

```bash
scripts/calibration/kalibr/build.sh --runtime-only
```

### 双目模式输出

- `stereo.bag`：ROS 1 图像 bag，仅保留 header 时间戳完全一致的左右图像对。
- `stereo.conversion.json`：输入数量、有效配对数、丢弃孤立帧数、分辨率和时间范围。
- `stereo-camchain.yaml`：双目内参、畸变、相机之间的外参。
- `stereo-results-cam.txt`、`stereo-report-cam.pdf`：标定摘要与报告。
- `target.yaml`、`container-image.json`、`command.txt`、两个日志：便于复现。

程序使用虚拟显示器并禁用弹窗，PDF 保存在宿主机输出目录，不需要 X11 转发。
容器退出码会传回调用方；失败时保留日志，不会把部分输出报告为成功。
源 bag 不变，输出归当前用户所有。

转换时保持图像像素、分辨率和 header 时间戳；ROS 1 bag 的索引时间使用图像
`header.stamp`，不使用网络到达时间。这样也能处理板端未校时导致采集时间为
1970 年、主机接收时间为当前年份的数据。此转换不修复曝光同步或时间戳精度。
重复/倒退时间戳、缺失话题、不符合 mono8 的图像、少于 20 对有效图像会报错。
20 对只是输入检查下限，不是标定质量保证。

仅验证并转换，不运行优化：

```bash
scripts/calibration/kalibr.sh /path/to/ros2-bag --convert-only
```

观察角点覆盖、重投影误差、基线是否接近设计的 50 mm，并用独立采集复核结果。
没有 IMU 消息的 bag 只能标定双目，不能得到相机–IMU 外参或时间偏移。
完整打印、摆放和采集方法见 [上级 README](../README.md)。

## 静止 IMU Allan 分析

```bash
scripts/calibration/kalibr.sh cali/sc7u22-static --allan
```

直接读取 ROS 2 IMU，无需相机话题或转包。可加 `--imu-topic`、`--skip-seconds`、
`--duration`、`--output`。输出六轴重叠 Allan 曲线 PNG/PDF/CSV、拟合 JSON、Markdown
报告和 Kalibr 格式 `imu.yaml`。算法脚本随结果存档，无需为此功能重建容器。
不改变原始数据、单位或滤波；使用 header 时间检查采样，统计窗口不跨越明显缺口。

自动拟合 −1/2 和 +1/2 斜率区间。没有足够证据的分量写 null，而非虚构四个完整参数。
`imu.yaml` 三轴汇总取最大值；报告标明时间范围、斜率、静止检查和数据长度限制。
详细算法和本次数据结果见 [上级 README](../README.md#一键-allan-分析静止-ros-2-bag)。

## 回归测试

测试使用合成数据和仅供测试的噪声参数，不产生实际标定结果：

```bash
podman run --rm --init --network=none \
  --volume "$PWD/scripts/calibration/kalibr:/tests:ro" \
  localhost/tidesense-kalibr:noetic python3 /tests/test_convert.py
```

覆盖 IMU/图像数值与时间戳保留、旧双目转换、缺失 IMU、时间不重叠、重复时间戳、
未填写噪声参数及分辨率不匹配。

同一命令将 `test_convert.py` 换为 `test_allan.py` 可验证 Allan 算法：直接窗口均值
计算对照、恒定偏置不变性、已知白噪声/随机游走系数恢复、错误斜率拒绝和时间缺口分段。

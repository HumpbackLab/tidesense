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
scripts/calibration/kalibr.sh cali/tidesense-stereo-calib
```

默认在 bag 同级创建 `tidesense-stereo-calib-kalibr-时间/`，原始 bag 只读挂载。
结果目录已存在时拒绝覆盖。指定输出目录：

```bash
scripts/calibration/kalibr.sh cali/tidesense-stereo-calib \
  --output cali/stereo-calibration-result
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
scripts/calibration/kalibr.sh cali/tidesense-imucam \
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

## 保留六面补偿的 IMU 内参重标定（2026-10-17）

已使用 `cali/tidesense-imucam/` 的联合标定数据，完成三组离线标定。
原始数据、旧标定、设备配置都未修改。标定结果是候选参数，尚未部署。
离线参数能显著改善关闭在线标定的 OpenVINS，但没有单独解决 SchurVINS 的发散。

### 输入与六面补偿核对

复用 `cali/tidesense-imucam-kalibr-20261011-173935/imucam.bag`，约 94.58 秒，
1753 对双目、18666 条 IMU。逐条核对原始 ROS 2 数据库与转换后的 ROS 1 bag：
全部 IMU header 时间戳和六轴数值完全一致。转换、重标定不再次应用六面补偿。

将 bag 的加速度反推既有六面补偿后，除以 IIO 单位尺度，全部样本回到整数采样格点，
最大误差约 `9.1e-13 LSB`；直接把 bag 当作未补偿数据时，中位格点误差约 `0.246 LSB`。
由此确认该 bag 已经使用与设备录像相同的六面补偿。新参数针对这个已补偿输入流。
参数来源和核对结果保存在各结果目录的 `preflight.json`、`original-bag-check.json`。

此 bag 有 160 个大于 7.5 ms 的 IMU 间隔，最大约 30.069 ms；中位间隔约 5.012 ms，
全段平均频率约 197.346 Hz。缺失样本未补造，原始时间戳未修改。
IMU 噪声使用原标定输入；独立 VIO 回放保留原设备噪声系数，没有按回放结果调参。

### 三组结果

| 组别 | 左/右平均重投影误差 px | 平均陀螺残差 rad/s | 平均加速度残差 m/s² |
|---|---:|---:|---:|
| radtan，标准 scale-misalignment | 0.755 / 0.757 | 0.001764 | 0.036752 |
| equidistant，标准 scale-misalignment | 0.423 / 0.417 | 0.001634 | 0.031326 |
| equidistant，联合重拟合时固定 A=0 | 0.446 / 0.440 | 0.001851 | 0.039529 |

结果目录分别为：

- `cali/tidesense-imucam-scale-misalignment-20261017-radtan/`
- `cali/tidesense-imucam-scale-misalignment-20261017-equi/`
- `cali/tidesense-imucam-scale-misalignment-20261017-equi-nogs/`

每组包含 `imucam-imu.yaml`、`imucam-camchain-imucam.yaml`、文本结果、PDF、
原始输入副本、输入/输出哈希及精确容器命令。前两组使用原版容器；标准模型还会估计
9 个陀螺加速度敏感参数 A。第三组通过可审查的单行 Python 源码覆盖，将 A 从零初始化
后固定不优化，其余参数重新联合求解。这是实验对照，不是 Kalibr 原生 CLI 选项。
原始与覆盖后的 `IccSensors.py` 及其哈希均保存于第三组目录，容器镜像未被修改。
前两组日志分别有 11、5 次迭代记录；第三组正常退出并生成全部结果，但捕获的 stdout
在准备阶段停止更新，因此不报告其迭代次数。

第三组的逆陀螺比例对角约为 `1.013613 / 0.990758 / 1.000003`；cam0 时间偏移约
`10.918641 ms`。必须同时使用配套相机–IMU 外参与时间偏移，保持标定时的坐标系定义。

### 独立录像验证

使用未参与离线拟合的 `dataset-20261012-084638` 和 `dataset-20261012-003648`。
所有指标为首末估计位置差，没有轨迹真值，不是 ATE。回放在电脑进行，FP64、无 ROS。
选用第三组 `equi-nogs` 候选后，同组对照固定相机、外参、时间偏移及视觉参数，
只切换是否加载离线 IMU 内参。OpenVINS 使用 1 px 视觉噪声和既有 IMU 启动静止门控，
始终关闭在线 IMU 内参和相机参数标定。

| 回放方式 | 084638 首末差 m | 003648 首末差 m |
|---|---:|---:|
| OpenVINS，新相机/时间，单位 IMU 内参 | 179.548 | 181.425 |
| OpenVINS，原生加载固定 Tw/Ta/R，关闭在线标定 | **1.356** | **2.923** |
| OpenVINS，外部预处理 IMU 后使用单位内参 | 0.975 | 4.716 |
| SchurVINS，新相机/时间，不修正 IMU | 351.731 | 757.302 |
| SchurVINS，外部预处理完整离线 IMU 模型 | 37.670 | 792.056 |

优先参考 OpenVINS 原生固定内参方式：直接加载 Tw/Ta/R，IMU 数值保留原来的六面补偿，
无需再预处理。两段录像的最大位移分别为 1.700、3.592 m，最大估计速度约 2.231、
3.485 m/s。第二段仍有米级漂移，不能据此认定为生产标定或宣称全程精度。

外部预处理实验保留原标量噪声系数和初始零偏协方差；原生固定模型则在传播里使用
内参及对应雅可比，因此两种回放的统计模型不完全等价，不能混用数值。
SchurVINS 当前不直接读取这些内参，表中使用的是显式输入预处理。不能据此把它与
OpenVINS 原生模型的全部剩余差异都归因于视觉前端。

标准模型中直接使用完整 A 项使独立回放明显恶化；在 equidistant 标准拟合后仅忽略 A，
OpenVINS 两段首末差为 0.253、38.285 m。第一段较小的闭合差并不能支持迁移，
也不能替代将 A 固定为零后重新联合拟合的对照。

共完成 36 次 SchurVINS 和 30 次 OpenVINS 回放，每个案例运行两次，所有重复位姿一致
（OpenVINS 排除耗时列）。所有输入帧均完整处理：084638 为 885 帧，003648 为 829 帧。
SchurVINS 分别输出 885、610 条位姿；OpenVINS 分别输出 647、616 条正常位姿，
其余为初始化/启动静止阶段，没有用删帧改善结果。冻结的全部数值和指纹在
[`intrinsics-results-20261017.json`](intrinsics-results-20261017.json)，原始输出和图在
`install/kalibr-intrinsics-20261017/`。

### 使用和复现

已有转换结果目录可使用新入口，输出必须是新目录：

```bash
python3 scripts/calibration/kalibr/calibrate_intrinsics.py \
  --source cali/tidesense-stereo-yy-equi-20261011-231002-imucam \
  --output cali/imu-intrinsics-new
```

复现固定 A=0 的联合拟合时，增加 `--disable-g-sensitivity`。该选项提取本机镜像中的
Python 模块，核对恰有一处激活语句，只读覆盖该模块来固定 A；原版模型保持默认。
已有 `kalibr.sh` 仍使用 `calibrated` 并自动导出传统 SchurVINS 配置，其行为没有改变。

独立回放示例：

```bash
install/dataset-test-venv/bin/python \
  scripts/calibration/kalibr/validate_intrinsics.py \
  --calibration cali/tidesense-imucam-scale-misalignment-20261017-equi-nogs \
  --recording install/replay-20261012-084638/record \
  --base-calibration install/fpv-stability/device-schurvins-20261017.yaml \
  --binary install/fpv-stability/build/schurvins_stereo \
  --output install/kalibr-intrinsics-20261017/reproduce-new
```

验证器按 Kalibr 前向模型求逆：
`a=inv(Ma)*am`，`w=C.T*inv(Mw)*(wm-A*C*a)`，由估计器继续估计残余零偏。
用独立的前向合成数据验证逆变换及零偏坐标变化；实际录像不减去标定样本拟合的零偏。
图像时间戳先撤销旧导出偏移，再应用新 cam0 偏移一次；不外推 IMU 或裁剪尾部帧。

候选目录另有：

- `residual-imu-correction.yaml`：逆模型与六面补偿来源，便于检查，原生 SchurVINS 尚不读取。
- `openvins-imu-fixed.yaml`：已验证的 OpenVINS 固定 Tw/Ta/R 格式，`model: kalibr`。
- `openvins-estimator-prealigned.yaml` / `openvins-cameras-prealigned.yaml`：对应已应用新偏移的回放输入。
- `openvins-cameras-raw-timestamps.yaml`：原始相机时间戳使用完整新偏移。
- `openvins-cameras-old-export.yaml`：已有 CSV 已包含旧 10.797086 ms 偏移时，只补剩余差。
- `integration.json`：输入处理与坐标系约定。OpenVINS 相机 YAML 包含右图 180° 虚拟相机变换，
  必须配合现有适配器的右图旋转使用。

现有 SchurVINS 导出器仍拒绝 `scale-misalignment`，不会静默丢弃内参生成生产配置。
本轮没有替换设备程序或 `/mnt/data/schurvins-gcc11-static-micro-ros/schurvins.yaml`。

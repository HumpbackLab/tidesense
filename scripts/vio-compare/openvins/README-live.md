# TideSense 实时 OpenVINS + micro-ROS 位姿

设备包：`/mnt/data/openvins-live-fp64/`。使用 A53、GCC11、**完整 FP64 后端**：
`P Hᵀ` 使用原始双精度乘法，协方差存储、求解和写回也使用 FP64。
构建时关闭局部与完整 FP32 实验，运行时强制 `OPENVINS_EKF_MA_MODE=original`。
默认 `num_pts=150`，双目每目补点目标约 75；16×15 LK 窗口和失败点过滤开启。
运行时可关闭 micro-ROS，独立运行 OpenVINS。

## 当前标定与传感器

配置由设备上的 `/mnt/data/schurvins-gcc11-static-micro-ros/schurvins.yaml` 转换。
转换副本位于包内 `config/`，`calibration-source.json` 记录来源、SHA256 和转换方式。
不能直接把 SchurVINS YAML 当作 OpenVINS 配置传入。

- OV9281 双目 640×400、20 Hz；SC7U22 IIO 200 Hz，沿用 INT1 中断时间戳与
  每 10 次中断触发一次 FSIN 的内核实现。
- 复用 SchurVINS 的相机辅助进程和 IIO 读取层；自动曝光上限仍为 2 ms。
- 六面加速度校准从 `/mnt/data/penguin-flight-console/accelerometer-calibration.json`
  加载，在 IMU 输入端应用一次。日志应显示 `six-face accelerometer calibration applied`。
- SchurVINS 的 `T_B_C` 取逆得到 `T_cam_imu`。右图旋转 180°，其内参、切向畸变
  和外参转换到相应虚拟相机；不要再在摄像头寄存器里旋转一次。
- 相机输入保持原始左图 VI PTS。标定的 `live_camera_time_offset_ns=10797086`
  转为 OpenVINS `timeshift_cam_imu=0.010797086`，由滤波器应用一次；IMU 读取
  等待覆盖这个对齐后的时刻。四个 IMU 噪声参数直接复制，不再额外乘十。
- 相机内外参与时延固定使用标定值。开启 `try_zupt=true`、
  `zupt_only_at_beginning=true`，使启动静止时可初始化；首次运动后停止使用 ZUPT。
  保持静止约 3 秒，并让两颗镜头共同看到有纹理的场景，再开始移动。
  图像纹理不足或启动时晃动可能延长等待时间。实时入口检查滤波器实际初始化状态，
  静止 ZUPT 阶段也可发布位姿，不必等到首次常规视觉更新。

## 1. 电脑启动 Agent

电脑 USB RNDIS 地址为 `192.168.123.2`，设备为 `192.168.123.99`。
端口 8888 已有 Agent 时复用，避免重复启动。

```bash
cd /home/ncer/sophpi
source scripts/calibration/ros2-env.sh
export FASTRTPS_DEFAULT_PROFILES_FILE=$PWD/scripts/schurvins/micro_ros/calibration-agent.xml
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

ROS domain 为 0。以上 DDS 配置供 Agent 所在电脑本地订阅；在其他电脑订阅需配置发现网络。

## 2. 设备启动

先停止已有 SchurVINS、标定采集或 IMU 测试，并关闭 PFC IMU 预览，让相机与
IMU 释放。程序对相机和 IMU 使用独占锁，不会主动终止其他用户进程。
确认设备 `date` 正确；ROS 消息时间戳由采样的单调时钟映射到设备系统时间，
不自动同步电脑时钟。板端执行：

```sh
cd /mnt/data/openvins-live-fp64
output=/mnt/data/openvins-live-$(date +%Y%m%d-%H%M%S)
mkdir -p "$output"
export OPENVINS_MICRO_ROS=1
export OPENVINS_MICRO_ROS_AGENT=192.168.123.2
export OPENVINS_MICRO_ROS_PORT=8888
export OPENVINS_MICRO_ROS_DOMAIN=0
export OPENVINS_IMU_CALIBRATION=/mnt/data/penguin-flight-console/accelerometer-calibration.json

nohup ./run-live.sh config/estimator_config.yaml /mnt/data/sensor_cfg.ini \
  auto "$output/poses.csv" 0 >"$output/vio.log" 2>&1 </dev/null &
echo $! >/tmp/openvins-live.pid
echo "$output" >/tmp/openvins-live-output
echo "OpenVINS PID: $(cat /tmp/openvins-live.pid); output: $output"
```

参数为配置、sensor INI、IIO 设备、输出 CSV、处理帧数；`auto` 查找 SC7U22，
`0` 表示持续运行。程序会显式选择 `OPENVINS_EKF_MA_MODE=original`。
日志出现 `openvins: INITIALIZED` 后开始发布位姿。日志每 20 个处理帧输出一次状态。

```sh
# 查看运行状态
tail -f "$output/vio.log"
# 重新登录设备后查看当前日志
tail -f "$(cat /tmp/openvins-live-output)/vio.log"

# 正常停止，释放相机、FSIN 和 IIO 缓冲
kill -INT "$(cat /tmp/openvins-live.pid)"
```

关闭 ROS 通信时使用 `OPENVINS_MICRO_ROS=0`，其余启动命令相同。
Agent 断开不会阻塞 VIO，通信线程会重连。

## 3. 电脑订阅位姿

每个新终端先加载环境：

```bash
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
ros2 topic echo /tidesense/pose geometry_msgs/msg/PoseStamped \
  --qos-reliability best_effort --no-daemon

# 在另一个已加载环境的终端查看状态
ros2 topic echo /tidesense/tracking std_msgs/msg/Bool \
  --qos-reliability best_effort --no-daemon
```

- `/tidesense/pose` 为 **IMU/body 到 odom** 的位姿，位置米、Hamilton 四元数 xyzw，
  `header.frame_id=odom`。OpenVINS 内部 JPL 四元数通过旋转矩阵转换，避免方向颠倒。
- 发布的是视觉更新后的位姿，按实际处理帧率输出；此入口没有 200 Hz IMU 预测发布。
  相机仍采集 20 Hz，计算跟不上时保留最新双目帧，全部 IMU 样本继续保留并输入。
- 此处 `/tidesense/tracking=true` 表示已初始化、当前相机时刻的滤波状态成功更新且
  数值检查通过；**不是**独立的视觉跟踪质量或精度保证，静止 ZUPT 更新也算成功更新。
  未初始化或当前状态未更新时不发布位姿。Agent 断连时接收端应检测消息超时。
- 话题与 SchurVINS 相同；不要同时运行两个发布器。此入口不发布图像、TF、IMU 消息或协方差。

CSV 记录原始相机与对齐后 IMU 时刻、帧序号、初始化/更新状态、body 到 odom 位姿、
相机处理耗时、整帧处理耗时、消费时图像年龄和后端观测数。
`camera_ms` 只覆盖 `feed_measurement_camera()`；不包括外部 IMU 输入、图像旋转和通信。
`sequence_gaps` 与停止时的 `replaced_pending` 可检查视觉消费降帧，停止摘要包含实际消费帧率和 IMU 采样率。
状态行的 `tracks=L/R` 为两目当前跟踪点数，`zupt=1` 表示该帧使用了静止更新。
需要检查实际画面时，启动前设置 `OPENVINS_SNAPSHOT_DIR` 为已创建的目录；
程序在第 100 个处理帧保存 `left.png`、`right-rotated.png` 和 `tracks.png` 一次。
正常运行不设置这个变量，避免图像保存影响耗时。

## 构建与更新标定

在 SDK 本机执行；需要现有 GCC11、OpenCV/yaml-cpp、micro-ROS 静态库：

```bash
JOBS=2 scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
python3 scripts/vio-compare/openvins/build_live.py

sshpass -p cvitek scp -O \
  root@192.168.123.99:/mnt/data/schurvins-gcc11-static-micro-ros/schurvins.yaml \
  install/openvins-live-fp64/schurvins-device.yaml
python3 scripts/vio-compare/openvins/make_live_config.py \
  --schur install/openvins-live-fp64/schurvins-device.yaml \
  --output-dir install/openvins-live-fp64/config --num-pts 150
```

`build_live.py` 校验 FP64 核心库，复用其编译参数和静态依赖，不修改核心算法。
输出 `bin/openvins_live.stripped`，部署时命名为 `bin/openvins_live`；
保持 `schurvins_camera`、`fsin.sh`、`prepare-imu.sh` 在同一 `bin/` 目录。
配置目录三个 YAML 必须一起上传。更新配置或二进制前正常停止设备上的本程序。

```bash
scp -O install/openvins-live-fp64/bin/openvins_live.stripped \
  root@192.168.123.99:/mnt/data/openvins-live-fp64/bin/openvins_live
scp -O -r install/openvins-live-fp64/config \
  root@192.168.123.99:/mnt/data/openvins-live-fp64/
```

构建时 `--micro-ros off` 可生成不带 ROS 通信库的版本；带库版本在运行时关闭
`OPENVINS_MICRO_ROS` 即可独立运行，无需重编。

## 精度恢复（2026-10-11）

按要求从局部 FP32 恢复到完整 FP64 后端。旧部署目录
`/mnt/data/openvins-live-mixed/` 中的运行程序和启动脚本也更新为同一 FP64 版本，
原先在该目录执行 `./run-live.sh ...` 的命令仍可使用；其 `pose.csv` 和诊断日志保留。
新的启动、构建和标定转换命令使用 `/mnt/data/openvins-live-fp64/`。

先前的局部 FP32 静止 ZUPT 验证只覆盖发布链路，随后实时运行出现过负协方差
对角线检查；不将那次测试作为 FP64 的验证结果或运动精度保证。
本次 FP64 的验证记录保存于本机 `install/openvins-live-fp64/`，
当前运行目录可在设备读取 `/tmp/openvins-live-output`。

FP64 上板静止验证：日志显示 `precision=fp64 original`，电脑实收 526 条
位姿、约 19.92 Hz；有限值、单位四元数和递增时间戳检查通过。
此结果验证静止初始化与通信，不代表运动场景的精度或处理帧率。

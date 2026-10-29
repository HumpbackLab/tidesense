# TideSense OpenVINS 实时模式

在 SG2002 A53 上运行 FP64 OpenVINS 双目惯性 VIO。输入为 OV9281 双目 640×400、约 20 Hz，以及 SC7U22 IMU 200 Hz。实时入口使用独立相机辅助进程和 IIO 读取层；运行前须停止其他相机与 IMU 消费者。micro-ROS 可在运行时关闭。

完整像素前端迁至 C906＋TPU 的可选版本，见[前端卸载说明](README-frontend.md)。

## 构建与标定

在 SDK 根目录构建 FP64 核心和实时入口：

```sh
JOBS=2 scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
python3 scripts/vio-compare/openvins/build_live.py
python3 scripts/vio-compare/openvins/make_live_config.py \
  --schur /path/to/this-board-schurvins.yaml \
  --output-dir install/openvins-live-fp64/config --num-pts 150
```

标定必须与实际板卡匹配。转换程序生成 OpenVINS 配置及相机、IMU 参数；右图的 180° 方向修正和相机与 IMU 时间偏移在转换后配置中处理，运行时不要重复应用。输出位于 `install/openvins-live-fp64/`。

部署到设备：

```sh
ssh root@192.168.123.99 'mkdir -p /mnt/data/openvins-live-fp64/bin'
scp -O install/openvins-live-fp64/run-live.sh root@192.168.123.99:/mnt/data/openvins-live-fp64/
scp -O install/openvins-live-fp64/bin/openvins_live.stripped \
  root@192.168.123.99:/mnt/data/openvins-live-fp64/bin/openvins_live
scp -O install/openvins-live-fp64/bin/{schurvins_camera,fsin.sh,prepare-imu.sh} \
  root@192.168.123.99:/mnt/data/openvins-live-fp64/bin/
scp -O -r install/openvins-live-fp64/config root@192.168.123.99:/mnt/data/openvins-live-fp64/
```

设备还需有匹配的 `/mnt/data/sensor_cfg.ini`，选择 OV9281 双目 640×400、硬件同步 20 Hz。

## 运行

不使用 ROS 时，在设备上运行：

```sh
cd /mnt/data/openvins-live-fp64
OPENVINS_MICRO_ROS=0 ./run-live.sh config/estimator_config.yaml \
  /mnt/data/sensor_cfg.ini auto /mnt/data/openvins-poses.csv 0
```

参数依次为 OpenVINS 配置、相机 INI、IIO 设备、位姿 CSV 和处理帧数；`auto` 查找 SC7U22，`0` 表示持续运行。启动时保持设备静止，并让双目共同看到有纹理的场景。按 Ctrl-C 正常退出，释放相机、FSIN 和 IIO。

发布 ROS 2 位姿时，先在电脑上启动 micro-ROS Agent：

```sh
source scripts/calibration/ros2-env.sh
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

然后在设备上设置 Agent 地址并启动：

```sh
OPENVINS_MICRO_ROS=1 OPENVINS_MICRO_ROS_AGENT=192.168.123.2 \
  ./run-live.sh config/estimator_config.yaml /mnt/data/sensor_cfg.ini \
  auto /mnt/data/openvins-poses.csv 0
```

`/tidesense/pose` 为 `geometry_msgs/msg/PoseStamped`，表示 IMU/body 到 odom 的位姿；`/tidesense/tracking` 为 `std_msgs/msg/Bool`。两者使用 best effort QoS。位姿按完成的视觉更新发布，不提供 IMU 频率的预测位姿。ROS 时间戳由设备单调时钟映射到系统时间，设备需先校时。Agent 断开后通信线程会重连，VIO 继续运行。

输出 CSV 包含相机与 IMU 时刻、帧序号、初始化状态、位姿和处理耗时。运行期间丢失跟踪或尚未初始化时，输出不代表有效定位；评估精度需结合真值和场景。
# 可选 C906/TPU LK

C906/TPU 卸载与前后端并行的独立构建、运行命令和完整回放结果见
[README-tpu.md](README-tpu.md)。常规 FP64 安装包和默认 CPU 路径保持可用。

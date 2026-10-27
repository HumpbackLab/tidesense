# TideSense SchurVINS

在 SG2002 的 Cortex-A53 上运行双目惯性 VIO。实时图像由 `tide_cam` 在进程内采集：两路 OV9281 默认 640×400、硬件同步约 20 Hz；SC7U22 IMU 以 200 Hz 提供中断时间戳。相机时间戳取 VI 到达时间，图像保持原始方向。程序支持独立运行、micro-ROS 位姿发布、现场录制和离线回放。

## 构建与部署

在 SDK 根目录运行：

```sh
JOBS=4 scripts/schurvins/build.sh
```

默认生成 `install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static-micro-ros-a53.tar`。将包复制到设备并解压到 `/mnt/data`。`build_all` 也会构建并预装此版本。

构建时可用 `SCHUR_ROS_BACKEND=micro-ros|native-dds|none` 选择通信后端；默认是 micro-ROS。三种后端使用独立构建目录。源码版本和依赖由 [sources.lock](sources.lock) 固定，构建入口会准备所需的 OpenCV、yaml-cpp、glog 等静态依赖。原生 DDS 的部署方式见 [native_dds/README.md](native_dds/README.md)。

随包的 `schurvins.yaml` 仅适用于对应设备。换板后需使用该板的相机内参、双目及相机与 IMU 外参、IMU 内参和时间偏移。TideConfigurator 可生成并应用标定结果；参数说明见 [CALIBRATION.md](CALIBRATION.md)。

## 实时运行

先停止其他相机和 IMU 采集程序。在设备上运行：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=0 ./run.sh --vio schurvins.yaml auto /mnt/data/poses.csv 0
```

`auto` 查找 SC7U22 IIO 设备，最后的 `0` 表示持续运行；按 Ctrl-C 正常停止并释放 FSIN、相机和 IIO。`poses.csv` 保存视觉校正位姿。启动时保持设备静止，并让双目看到有纹理的场景。曝光上限从设备 `camera.conf` 读取，详情见 [tide_cam](../../tide_cam/README.md)。

要通过 micro-ROS 发布位姿，先在电脑上启动 Agent：

```sh
source scripts/calibration/ros2-env.sh
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

随后在设备上指定电脑的 USB 网卡地址：

```sh
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --vio schurvins.yaml auto /mnt/data/poses.csv 0
```

`/tidesense/pose` 是 `geometry_msgs/msg/PoseStamped`，表示 IMU/body 到 odom 的位姿；`/tidesense/tracking` 是 `std_msgs/msg/Bool`。两个话题使用 best effort QoS。视觉校正约 20 Hz；初始化后可用 `SCHUR_PREDICTION=1` 按 IMU 采样率发布预测位姿。通信和时间戳语义见 [MICRO-ROS.md](MICRO-ROS.md)。

## 录制与回放

要保存实时 VIO 消费的输入，可在 `--vio` 命令末尾加 `--record NEW_DIR`。录制目录必须尚未存在；正常结束后可运行：

```sh
SCHUR_MICRO_ROS=0 ./run.sh --replay /mnt/data/vio-record /mnt/data/replay-poses.csv
```

只需录制数据集而不运行 VIO 时，使用 [独立录制器](../dataset/README.md)。录制文件和回放条件见 [RECORDING.md](RECORDING.md)。

程序也接受已配对图像与 IMU 的 CSV 输入：

```sh
./run.sh calibration.yaml stereo.csv imu.csv poses.csv
```

`stereo.csv` 行格式为 `timestamp_ns,left_image,right_image`；`imu.csv` 为 `timestamp_ns,gx,gy,gz,ax,ay,az`，单位依次为 rad/s 与 m/s²。时间戳必须属于同一时钟域、严格递增，IMU 覆盖完整相机区间。输入数据应匹配传入的标定和 `raw_si` IMU 模型。

相机、IMU 的硬件和接口细节分别见 [CAMERA.md](CAMERA.md)、[IMU.md](IMU.md)。性能实验与结果保存在 `bench/` 和 `tests/`，不作为默认运行参数。

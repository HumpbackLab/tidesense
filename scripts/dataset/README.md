# TideSense 独立双目/IMU 数据集录制

`tidesense_record` 不链接 SchurVINS、OpenVINS、ROS 或 DDS，不运行特征提取和 VIO。
复用经过板端验证的 CVITEK 双 OV9281 helper 和 SC7U22 IIO 读取代码。
双目 640×400 mono8 / 20 Hz；IMU 200 Hz，INT1 内核时间戳，10 次中断触发一次 FSIN。
两颗相机使用现有固定曝光/增益 ISP 配置，不旋转或去畸变图像；存储采用可逐字节还原的无损压缩。

## 编译与部署

```sh
scripts/dataset/build.sh
scp -O install/soc_sg2002_tidesense_arm64_sd/tidesense-dataset-a53.tar root@192.168.123.99:/mnt/data/
# 板端
cd /mnt/data
tar xf tidesense-dataset-a53.tar
```

首次构建依赖现有 `scripts/schurvins/build.sh` 建好的 GCC11 OpenCV/yaml-cpp 静态库；
录制程序本身单独编译，不构建/链接 VINS。构建脚本下载并校验
[Zstandard 1.5.7](https://github.com/facebook/zstd/releases/tag/v1.5.7) 的固定 SHA256，静态链接。相机 helper 仍使用 SDK GCC6/CVITEK 库，独立进程。

## 录制

先停止占用相机或 IIO 的 VIO/标定/PFC 采集程序。SD 必须挂载 async，不能 sync。

```sh
cd /mnt/data/tidesense-dataset
record_dir=/mnt/data/dataset-$(date +%Y%m%d-%H%M%S)
./run.sh --output "$record_dir" \
  --calibration /mnt/data/schurvins-gcc11-static-micro-ros/schurvins.yaml \
  >"$record_dir.log" 2>&1
```

前台运行，Ctrl-C 正常停止、排空写队列并同步到 SD。固定时长可加 `--seconds 120`。
输出目录必须不存在。`--sensor-ini` 默认 `/mnt/data/sensor_cfg.ini`，要求两路 `hw_sync=1`。
`--imu` 默认 auto。`--calibration` 可省略；它仅保存参数和保证 IMU 覆盖时间偏移，录制时不运行估计器。

默认读取 `/mnt/data/penguin-flight-console/accelerometer-calibration.json` 应用六面校准。
可用 `SCHUR_IMU_CALIBRATION=/path/file.json` 指定；有意录制未校准数据时设为 `none`。
同时保存校准前的 SI 加速度，`imu-calibration.yaml` 记录实际系数。回放勿再应用六面系数。

## 写盘与完整性

- 原始图像为 10.24 MB/s（约 614 MB/分钟，十进制）；本板 SD 实测约 9.18 MB/s，无法持续保存未压缩流。
- `frames.bin` 每对图做逐行 uint8 差分 + Zstandard level 1 无损压缩，再顺序写入，附带每对校验和；没有 JPEG 有损压缩或大量小文件操作。
- 压缩率取决于图像纹理/噪声，抽样约 2.22:1；极端高噪声若压缩后仍超 SD 带宽会明确报错，不静默丢帧。
- 写队列默认 64 对，约 32.8 MB；`--queue-pairs 1..128` 可调整。相机另有 8 对 FIFO。
- 队列满、SD 空间不足、相机参考号/帧序/时间戳跳变、IMU 间隔 >7.5ms 都报错并保留 `INCOMPLETE`。
- 正常停止才出现 `COMPLETE`；不能直接拔电。最终日志和 `metadata.yaml` 给出帧数、IMU 数、实际速率、最大间隔和队列峰值。
- 仅记录相机/IMU 完成启动后的连续区间。IMU 从第一帧前开始，到最后一帧后结束；不包含启动过程和停止后的空闲样本。
- 原始相机时间戳是 VI arrival PTS，**不是曝光时刻**。与 IMU 同属 monotonic 时钟；FSIN 同步不能消除读出延迟，回放仍需使用匹配当前曝光/触发方式的时空标定。

二进制格式：文件头 ASCII `TIDESENSE_ZST_V1\n`，随后重复：4 字节小端压缩块长度，
紧接一个独立带 checksum 的 Zstandard 帧。解压后是 56 字节小端
`<IIIIQQQIIII` CameraHeader（magic/version/width/height/sequence/left_ns/right_ns/left_ref/right_ref/flags/reserved），
紧接左图 256000 字节、右图 256000 字节，无行填充。图像字节是逐行差分：
首列原值，其余 `uint8(pixel[x]-pixel[x-1])`，按行累加模 256 还原。头部不做差分。
导出工具亦可读取早期 `TIDESENSE_RAW_V1\n` 未压缩格式。
IMU CSV：timestamp_ns,gx,gy,gz,ax,ay,az,raw_ax,raw_ay,raw_az，单位 rad/s、m/s²。

## 电脑端导出与回放

```sh
python3 -m venv .venv-dataset
.venv-dataset/bin/pip install PyYAML numpy zstandard rosbags==0.9.23
scp -O -r root@192.168.123.99:/mnt/data/dataset-YYYYMMDD-HHMMSS ./
.venv-dataset/bin/python scripts/dataset/export.py \
  dataset-YYYYMMDD-HHMMSS dataset-export --bag
```

同一次导出同时产生：

- `mav0/cam{0,1}/data/*.png` 和 `data.csv`：EuRoC 风格目录，无损 PNG，左右同步编号。
- `mav0/imu0/data.csv`：时间戳、角速度、已校准加速度。
- `stereo.csv`、`imu.csv`、`replay.yaml`：直接供本 SDK SchurVINS `--replay`。
- `dataset.bag`（`--bag`）：ROS1 原始 `sensor_msgs/Image` mono8 和 `sensor_msgs/Imu`，按时间排序。

`--calibration SCHUR.yaml` 可在导出时补充/替换参数；没有参数时可以导出 EuRoC/bag，
但不生成 `replay.yaml`。不提供虚假的 EuRoC sensor.yaml 或 groundtruth；算法配置需使用本板的真实标定。

导出默认将标定文件的 `live_camera_time_offset_ns` 加到 cam0 PTS，双目共用该帧时刻；
原始左右 PTS 另外保存到 `frames.csv`。`replay.yaml` 清零时间偏移，避免重复应用。
OpenVINS/VINS-Fusion 配置也须设置与导出一致的偏移（已经平移的 bag 应从 0 开始）。
如需重做 Kalibr，使用 `--time-offset-ns 0` 导出未平移时间戳，避免使用旧标定偏移。
右摄像头旋转 180° 已由真实外参表达，程序不会转图。

### SchurVINS

```sh
SCHUR_MICRO_ROS=0 ./run.sh --replay /path/dataset-export /tmp/replay-poses.csv
```

### OpenVINS / VINS-Fusion

两者官方都有 EuRoC ROS bag 示例：
[OpenVINS](https://docs.openvins.com/gs-tutorial.html)、
[VINS-Fusion](https://github.com/HKUST-Aerial-Robotics/VINS-Fusion#3-euroc-example)。
使用本板的内参、畸变、双目/IMU 外参、噪声参数构建各自配置，勿直接套用 EuRoC 的传感器参数。
配置 topic 为 `/cam0/image_raw`、`/cam1/image_raw`、`/imu0`，IMU 200Hz，双目模式。
算法启动后，在 ROS1 环境播放：

```sh
rosparam set use_sim_time true
rosbag play --clock dataset-export/dataset.bag
```

此工具输出 ROS1 bag；ROS2 环境需再转 ROS2 bag 或使用桥接，不是 `ros2 bag play` 可直接读取的 db3。
时间戳保留设备 monotonic 纳秒，不伪装成日历时间。无 IMU 姿态估计，orientation_covariance[0]=-1。

板端实测与错误路径检查见 [VALIDATION.md](VALIDATION.md)。

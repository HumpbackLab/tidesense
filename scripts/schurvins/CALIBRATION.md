# TideSense 双目 ROS 标定采集模式

实测结果与当前 WSL 环境限制见 [CALIBRATION-VALIDATION.md](CALIBRATION-VALIDATION.md)。

`--calibrate` 只采集并发布左右 OV9281 原始灰度图，不创建 SchurVINS 估计器，
不采集 IMU，不需要预先存在相机标定参数。没有 TCP 图像桥接器；两路图像直接
通过 micro-ROS / XRCE-DDS / USB RNDIS 到达电脑端 Agent。

## 构建与启动

使用 `SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh` 构建可选 micro-ROS 包。
这个开关仍默认 OFF；普通 VIO 的 micro-ROS 通信仍默认关闭。

先在电脑启动 Humble Agent（沿用 [MICRO-ROS.md](MICRO-ROS.md) 的安装与环境配置）：

```sh
export FASTRTPS_DEFAULT_PROFILES_FILE=/path/to/package/calibration-agent.xml
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

在板端解压后的程序目录运行，`192.168.123.2` 必须替换为实际电脑 RNDIS 地址：

```sh
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 5
```

发布 10 对/秒，选择 300 对输入后退出，同时保留时间戳诊断：

```sh
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 10 \
  --frames 300 --timestamps /tmp/calibration-stamps.csv
```

- `--fps` 必填，支持 0.1–20 的小数，单位为**图像对/秒**。
- 相机仍按 FSIN 20 Hz 采集；根据采集时间成对抽帧，不改变相机曝光或硬件触发频率。
  不能整除 20 Hz 的目标频率会有采样间隔交替；网络过载会降低实际收到的帧率。
- `--frames` 可选，默认 0（一直运行，Ctrl-C 退出）；计数为选中的输入图像对，
  不保证同等数量的 ROS 投递。启动发现、网络中断、退出时都可能少收。
- `--timestamps` 可选，保存被选中图像对的采集序号、左右 VI 原始时间戳、
  ROS 图像对时间戳以及是否进入发送邮箱。`queued=1` 不等于已被订阅者收到。
- 两颗相机必须都配置 `hw_sync=1`。启动前停止其他相机程序。
- `SCHUR_MICRO_ROS=0` 或未启用编译支持时，标定模式明确报错，不会启动相机。
  正常独立 VIO 继续使用原来的 `--vio` 等命令。

## 本机 ROS 2 预览和录制

沿用已验证的 Cyclone DDS 订阅端（Agent 内部仍为 Fast DDS）：

```sh
export CYCLONEDDS_URI=file:///path/to/package/calibration-cyclone.xml
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_DOMAIN_ID=0
ros2 daemon stop
ros2 run rqt_image_view rqt_image_view
```

将 `/path/to/package` 替换为电脑上解压的 micro-ROS 程序包目录；SDK 中原文件位于
`scripts/schurvins/micro_ros/`。录包终端也需设置相同的 `CYCLONEDDS_URI` 和 RMW。
这两个 XML 用于 **Agent 和 ROS 订阅者在同一台电脑** 的场景，DDS 使用 loopback
单播，数据包上限 1400 字节，并增大 socket 缓冲；板端 XRCE 仍通过 RNDIS 访问 Agent。
需要跨电脑 DDS 发现时，应另行调整 XML 的接口和 peers。
此配置要求主机允许至少 2 MiB 接收缓冲；本次主机 `net.core.rmem_max=4194304`。
若其他电脑的上限不足，可按需调整 `net.core.rmem_max` / `net.core.wmem_max` 后使用。

在预览工具中选择以下话题；类型均为 `sensor_msgs/msg/Image`：

| 话题 | 对应相机 | 编码 / 尺寸 | frame_id |
| --- | --- | --- | --- |
| `/tidesense/left/image_raw` | 左，0x10，2 lane | mono8 / 640×400 | left_camera_optical_frame |
| `/tidesense/right/image_raw` | 右，0x60，1 lane | mono8 / 640×400 | right_camera_optical_frame |

```sh
ros2 bag record -o tidesense-stereo-calib \
  /tidesense/left/image_raw /tidesense/right/image_raw
```

图像是未去畸变、未有损压缩的 VI Y 平面。图像发布采用 reliable、volatile、
keep-last depth 1；reliable 和 best-effort 订阅者均可匹配。
标定模式不会发布 pose/tracking，不伪造 CameraInfo 内参，也不发布 TF。

Kalibr 的常用 ROS 1 工作流需要兼容 `.bag`。ROS 2 录包后需转换，或导出带采集
时间戳的图像后使用 Kalibr bagcreater；不能把 ROS 2 目录直接当成 ROS 1 bag。
参见 [Kalibr bag format](https://github.com/ethz-asl/kalibr/wiki/bag-format)。
本 SDK 的 A4 AprilGrid 和配套 YAML 在 `scripts/calibration/`。

## 时间戳与配对

- 相机必须启用 FSIN；采集辅助程序仍按原有 VI 时间戳规则配对。
- 两张 Image **使用同一个图像对时间戳**，取左图 VI 到达时间，加上启动时的
  `CLOCK_REALTIME - CLOCK_MONOTONIC`。订阅端可按相同 stamp 配对。
- 左右 VI 到达时间不同不代表曝光不同；上述公共 stamp 也不是曝光中心或已测量的
  FSIN 边沿时间。原始左右 VI 时间保留在可选 CSV 中，结束日志报告最大到达时间差。
- 上述配对依赖相机硬件触发及现有采集配对规则，不构成独立的曝光同步精度验证。
- 录制前设置并同步板端时钟；该程序不会自动对时。系统时钟在运行中跳变不改变采集映射。
- 两个话题不是原子投递；网络中断可能只收到其中一张，使用精确 stamp 匹配并丢弃孤立图像。

## 缓冲与恢复

图像通过单个通信线程发送；采集线程持续接收硬件输出。应用发送邮箱最多保留
一对待发图像，过载覆盖旧对。图像缓冲预分配；不把每一帧保存在内存队列里。
XRCE UDP MTU 为 1400，可靠输出历史为 32 个 MTU，输入历史为 4。
固定版本的 RMW 支持 `uxr_prepare_output_stream_fragmented` 连续分片：
单张 256000 字节图像分批通过 44800 字节输出缓冲发送和确认。
实测此配置优于一次缓存整张图像的大窗口配置，后者在当前链路上造成更多等待。
每次可靠流确认等待配置为 500 ms（不是整张图像的最坏时间上界）。

Agent 缺席时采集仍能停止；连接断开后后台重新创建发布者。
本通道用于实时采样，无法补录断线期间的所有图像，也不保证任意网络下达到请求帧率。

## 验证工具

`tests/calibration_receiver.py` 检查 ROS Image 元数据、完整图像长度、递增时间戳，
按公共 stamp 配对，并保存第一张左右 PGM。`--mock` 还验证每个像素和左右对应关系。
`tests/board-calibration.py` 自动上传到临时目录，运行真实采集或 `--mock` 指定的
相机辅助程序，支持 `--fps`、`--frames` 和 `--restart`（重启 Agent）。

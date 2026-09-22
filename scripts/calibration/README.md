# TideSense A4 双目标定板与 Kalibr 标定流程

打印文件：[tidesense-aprilgrid-a4-6x4-32mm.pdf](tidesense-aprilgrid-a4-6x4-32mm.pdf)。
Kalibr 配置：[tidesense-aprilgrid-a4-6x4-32mm.yaml](tidesense-aprilgrid-a4-6x4-32mm.yaml)。
PNG 仅用于预览，请打印 PDF。

## 使用 Podman 一键标定已录制的 ROS 2 bag

容器构建与自动转换/标定脚本见 [Podman / Kalibr 使用说明](kalibr/README.md)。
主机不需要安装 ROS 1：

```bash
# 仅首次搭建需要构建；当前机器已构建完成。
scripts/calibration/kalibr/build.sh
```

## 规格

- A4 横向，297 × 210 mm，单页矢量图。
- AprilGrid：6 列 × 4 行，tag36h11，ID 0–23。
- 使用 Kalibr 官方生成器的两格黑边和对称角点黑方块。
- ID 0 在左下角；同一行向右递增，再向上一行。
- 单个标签黑色外边缘之间的边长：32 mm；标签间距：9.6 mm。
- `tagSize: 0.032`（米），`tagSpacing: 0.3`（9.6 / 32，无单位）。
- 标签阵列外边缘范围：240 × 156.8 mm；包含外围角点黑方块：259.2 × 176 mm。
- 图案居中，左右留白 18.9 mm，上下留白 17 mm；文字和尺标位于留白内。

## 打印与使用

1. 选择 **A4、横向、实际大小 / 100%**。关闭“适合页面”、缩放、海报拼接。
2. 使用清晰黑白打印，贴在平整、坚硬的哑光板上，避免翘曲、气泡和反光覆膜。
3. 打印后量取底部两刻线间距，应为 **100 mm**；再检查标签宽、高均为 **32 mm**。
   长距离复核可测整排标签的左右黑边，长度应为 **240 mm**。
4. 如果存在均匀缩放，应重新按原尺寸打印，或把 YAML 的 `tagSize` 改为实测米数；
   等比缩放时 `tagSpacing` 仍为 0.3。若横纵比例不同或纸张不平，请重新制作。
5. 两颗 OV9281 共用这一张板。用实际 VIO 所采用的分辨率、裁剪及固定镜头设置采集标定数据。
   让左右相机都有清晰可辨的共同标签，并采集不同位置、倾角和距离，覆盖画面中央与边缘。
   A4 的可用距离取决于镜头视场、对焦和图像中标签像素数，不能仅凭 50 mm 基线确定。
6. Kalibr 命令中设置 `--target` 为上述 YAML。采集场景中隐藏其他 AprilTag。

## 标定前先确定图像方向

前半部分完成双目内参、畸变和外参标定；后面的“相机–IMU 联合标定”章节说明
如何使用 `--imu auto` 同步录制 IMU，进一步估计相机–IMU 外参和时间偏移。

- 左相机：I2C 7 位地址 `0x10`、2 lane；右相机：`0x60`、1 lane；设计基线 50 mm。
- 如果一颗绕光轴倒装 180°，但两颗仍朝向同一方向，Kalibr 可以直接标定一正一倒的图像。
  倒装会体现在双目旋转外参中，不需要为了 Kalibr 专门修改寄存器。
- 也可以先把倒装相机的输出转正，再采集标定数据。180° 对应水平和垂直同时翻转，
  只翻转一个方向会成为镜像。具体 OV9281 寄存器配置需按手册与驱动确认，本流程不修改寄存器。
- **先选定最终运行时的图像方向、分辨率、裁剪和镜头焦点，再标定。** 标定与 SchurVINS
  运行必须使用相同图像配置；标定后改方向或裁剪，应重新标定，不能直接沿用旧参数。
  Kalibr 支持倒装不代表当前 SchurVINS 的匹配实现已验证支持该安装方式。

## 标定板放多远、怎么移动

以下距离和像素数是针对这张 A4 板、640×400 图像的**采集建议起点，不是镜头规格或已实测的有效范围**。
尚未给定镜头视场和最近清晰距离，应同时观察左右图像调整：

| 项目 | 建议 |
| --- | --- |
| 初始位置 | 板面距镜头约 **40–60 cm**，先正对双目共同视野中央 |
| 远近变化 | 清晰且可见的前提下，尝试约 **30–80 cm**；太近失焦/出框就后退，太远标签太小就靠近 |
| 图案大小 | 正对时让 240 mm 宽的标签阵列约占画面宽度 **50%–80%**；不要只拍中央一小块 |
| 标签清晰度 | 单个标签外黑边边长尽量达到 **约 30–40 像素或更多**，这是操作参考，不是检测器硬阈值；最终看实际角点检测 |
| 倾斜 | 围绕水平轴、竖直轴分别尝试约 **±15°、±30°**，再加入画面内旋转；避免侧得太厉害导致标签变窄 |
| 采集时长 | 先用 **5 对/秒采集 60–120 秒**，约 300–600 对输入；姿态覆盖比重复帧数更重要 |

固定 TideSense，移动标定板。推荐顺序：

1. 中央正对，调整距离和照明，让两颗相机都能看到清晰的共同标签。
2. 把板缓慢移到画面的左、右、上、下和四角，覆盖两颗相机的边缘区域。
   边缘采样可让部分板出框，但需要足够标签可检测，并保留大量两路共同观测。
3. 在不同位置加入左右倾斜、上下倾斜和少量画面内旋转；不要全程板面平行于相机。
4. 再改变远近距离，重复若干倾斜姿态。移动到一个姿态后短暂停稳约 0.5–1 秒，减少运动模糊。
5. 保持均匀照明，避免黑块反光、白纸过曝、手遮住标签或纸板弯曲；背景隐藏其他 AprilTag。

若已知水平视场角 θ，正对时可粗估距离：`d ≈ 0.240 / (2 × q × tan(θ/2))`，
其中 q 是标签阵列占图像宽度的比例。公式未考虑畸变和倾斜，只帮助找初始位置；
最终以双目预览的清晰度、共同视野和 Kalibr 检测结果为准，不能由 50 mm 基线推算拍摄距离。

## 1. 准备程序和主机环境

以下使用 **本机 ROS 2 Humble + micro-ROS Agent** 采集，随后在独立的 ROS 1 / Kalibr
环境中处理。主机需已安装 Agent、`rqt_image_view`、`rosbag2` 和 `rmw_cyclonedds_cpp`；
Agent 安装与可选构建说明见 [MICRO-ROS.md](../schurvins/MICRO-ROS.md)。
本机与板端通过 USB RNDIS 相通：板端 `192.168.123.99`，示例本机地址 `192.168.123.2`。
**Agent 地址填写本机的实际 RNDIS IPv4，不是板端 `.99`。**

如尚未构建，在 SDK 根目录执行：

```sh
cd /home/ncer/sophpi
SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh
```

将生成的 micro-ROS 程序包解压到板端；以下假设程序目录为
`/mnt/data/schurvins-gcc11-static-micro-ros`，相机配置为 `/mnt/data/sensor_cfg.ini`。
按实际部署路径修改。两颗相机都必须启用 `hw_sync=1`，启动前停止其他占用相机的程序。
录制前确认板端时间正确；标定过程中不改相机方向、裁剪和焦点。

以下主机命令使用 SDK 中的 XML；若主机没有 SDK，可改用程序包中同名 XML 的绝对路径。
XML 适用于 **Agent、预览和录包在同一台电脑**：电脑内 DDS 使用 loopback，
板端 XRCE 使用 RNDIS。主机需允许至少 2 MiB socket 接收缓冲，具体网络要求见
[采集模式文档](../schurvins/CALIBRATION.md)。

## 2. 本机启动 Agent

本机终端 A（先 source 已安装 Agent 的工作空间，如适用）：

```sh
export ROS_DOMAIN_ID=0
export FASTRTPS_DEFAULT_PROFILES_FILE=/home/ncer/sophpi/scripts/schurvins/micro_ros/calibration-agent.xml
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

## 3. 板端启动双目采集

SSH 登录板子后，在板端执行：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  SCHUR_MICRO_ROS_DOMAIN=0 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 5
```

`--fps` 是图像对/秒，必填，支持 0.1–20；默认持续运行，Ctrl-C 退出。
相机硬件仍以 FSIN 20 Hz 采集，程序成对抽帧发布；不会启动 VIO，未传 `--imu` 时不启动 IMU。
如需有限采集并保存原始 VI 时间戳诊断，可改为：

## 4. 本机预览、检查、录包

```sh
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
```

终端 B 打开预览；可开两个窗口分别选择左右话题：

```sh
ros2 daemon stop
ros2 run rqt_image_view rqt_image_view
```

| 话题 | 相机 | 图像 |
| --- | --- | --- |
| `/tidesense/left/image_raw` | 左，0x10，2 lane | `sensor_msgs/msg/Image`，mono8，640×400 |
| `/tidesense/right/image_raw` | 右，0x60，1 lane | `sensor_msgs/msg/Image`，mono8，640×400 |

新终端设置上述 ROS 2 环境后，可分别运行 `ros2 topic hz /tidesense/left/image_raw`
和 `ros2 topic hz /tidesense/right/image_raw` 检查接收频率。确认两路持续更新、清晰后录包：

```sh
mkdir -p /home/ncer/tidesense-calibration
cd /home/ncer/tidesense-calibration
ros2 bag record --storage sqlite3 -o tidesense-stereo-calib \
  /tidesense/left/image_raw /tidesense/right/image_raw
```

一键标定双目：
```scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-stereo-calib```


## 相机–IMU 联合标定

### 联合采集与录包

先完成上述双目标定，保留 `camchain-*.yaml`。确认板上已经安装 SC7U22 IIO/INT1 内核和
新版 micro-ROS 程序包。Agent 和主机 ROS 2 环境设置沿用前文。

板端运行（推荐图像 20 对/秒、IMU 200 Hz）：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 20 --imu auto \
```

`--imu auto` 启用 IMU，也可指定 `--imu /dev/iio:device0`。不加此参数则保持纯双目模式。
`--fps` 只改变图像发布频率，IMU 固定为 200 Hz。

主机新终端：

```sh
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
cd /home/ncer/tidesense-calibration
ros2 bag record --storage sqlite3 -o tidesense-imucam \
  /tidesense/left/image_raw /tidesense/right/image_raw /tidesense/imu
```

### 板子怎么运动

这一步**固定 AprilGrid，手持整个 TideSense 运动**。先从约 40–60 cm 的清晰共同视野开始，
实际距离按前文的画面占比和标签像素大小调整。镜头、图像方向、裁剪与双目标定时保持一致。

- 建议录制约 60–120 秒，开始和结束各留几秒静止数据。
- 在标定板持续可见的前提下，分别绕三个轴做平滑往返旋转，再加入各轴的平移和组合运动。
- 幅度以保持可见、清晰为准，避免只绕一个轴、只做平面运动或全程静止。
- 不要猛烈抖动或撞击；提高均匀照明、减少运动模糊。

20 Hz 图像 / 200 Hz IMU 与多轴激励依据
[Kalibr 官方联合标定说明](https://github.com/ethz-asl/kalibr/wiki/Camera-IMU-calibration)。
上述距离和时长是采集起点，仍需通过角点覆盖和结果报告判断数据质量。

### IMU 参数与 Kalibr 命令

#### 一键 Allan 分析静止 ROS 2 bag

```sh
scripts/calibration/kalibr.sh cali/sc7u22-static --allan
```

建议传感器预热后，保持板子和温度稳定，延长静止记录以观察长时间漂移。
[Kalibr 官方](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model)建议约 15–24 小时静止数据

#### 使用噪声参数进行联合标定

```sh
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-imucam \
  --imu-cam \
  --cam ~/sophpi/cali/tidesense-stereo-calib-kalibr-20260922-001523/stereo-camchain.yaml \
  --imu ~/sophpi/cali/sc7u22-imu.yaml
```

替换 `--cam` 为双目标定结果、`--imu` 为已填好实测噪声参数的文件。
相机话题、模型和分辨率从 camchain 读取，IMU 话题从 IMU YAML 的 `rostopic` 读取；
联合模式不再传 `--topics` / `--models`。可用 `--target` 指定实际打印的标定板。

脚本自动校验并转换 ROS 2 bag，再运行 `kalibr_calibrate_imu_camera --imu-models calibrated`。
转换保留所有 IMU 样本及 header 时间戳；图像仅保留时间戳精确匹配的左右帧对，
bag 索引时间取各自 header 时间，不取网络接收时间。检查分辨率与已有内参是否一致、
IMU 时间戳递增且与图像重叠，拒绝噪声参数为 null 或非正数的模板。
不会自动校正 IMU 零偏/尺度，也不旋转图像或 IMU 坐标轴。

## 来源、重建与验证

编码、位顺序、黑边及角点布局依据 [Kalibr 官方生成器](https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/aslam_offline_calibration/kalibr/python/kalibr_create_target_pdf)。
参数定义见 [Kalibr calibration targets](https://github.com/ethz-asl/kalibr/wiki/calibration-targets)。
本目录脚本调整了 A4 页面布局，并加入尺寸核对尺。

安装 PyMuPDF 后可重建：

```sh
python3 scripts/calibration/generate_aprilgrid_a4.py
```

已验证：PDF 为单页 A4 横向；300 dpi 渲染后，OpenCV 4.10 的 AprilTag 36h11
检测器（`markerBorderBits=2`）识别出全部 24 个 ID，且位置与 YAML 的行列顺序一致。
这验证了电子文件，不代表已验证实际打印误差或实拍标定精度。

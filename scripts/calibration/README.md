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
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-stereo-calib
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
source /opt/ros/humble/setup.bash
# 若 Agent 从源码安装，在这里 source 对应工作空间的 install/setup.bash。
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

```sh
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  SCHUR_MICRO_ROS_DOMAIN=0 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 5 \
  --frames 600 --timestamps /tmp/calibration-stamps.csv
```

`--frames 600` 约运行 120 秒，计数是选中的输入图像对，不保证录包收到 600 对。
若使用有限采集，建议先启动下面的录包终端，避免录制开始前就消耗输入帧数。

## 4. 本机预览、检查、录包

本机终端 B，以及每个后续 ROS 2 订阅/录包终端，都先执行：

```sh
source /opt/ros/humble/setup.bash
export ROS_DOMAIN_ID=0
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file:///home/ncer/sophpi/scripts/schurvins/micro_ros/calibration-cyclone.xml
```

本 SDK 也提供环境脚本，可代替上面的环境设置，在**每个预览和录包终端**执行：

```sh
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
```

脚本优先加载 SDK 中已有的 `install/micro-ros/host-cyclone` 依赖（若存在），
解决本机只 source `/opt/ros/humble/setup.bash` 时找不到 Cyclone RMW 的问题；
没有该目录时，需要主机已安装 `rmw_cyclonedds_cpp`。脚本不加载之前测试用的网络替代库。
Agent 仍按上一节单独启动。只在 Agent 终端设置环境不会影响另一个录包终端。

录包出现 `Recording...` 仅表示进程开始运行；应进一步看到两个图像话题的
`Subscribed to topic` 日志。若 bag 为 `Messages: 0`、话题列表为空、时间显示 2262 年，
这是空包的时间占位值，不代表相机真的产生了该时间戳。先检查板端程序和 Agent 是否运行，
再检查录包终端的 RMW、domain 和 XML 配置是否与这里一致。空包没有可恢复的图像，需要重新录制。

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

按前述摆放流程采集，完成后在录包终端按 Ctrl-C，等录包正常关闭，再停止板端采集和 Agent。
输出目录不能已存在，重复采集请换名字。5 对/秒的原始像素约 2.56 MB/s，
两分钟约 307 MB，另有 bag 开销，提前留出磁盘空间。

```sh
ros2 bag info /home/ncer/tidesense-calibration/tidesense-stereo-calib
```

确认两路话题都有足够消息，计数接近；计数相等本身不证明逐帧同步。
程序给同一对图像相同的 `header.stamp`，来自左 VI 到达时间映射到系统时间，
不是经过测量的曝光中心时间。断线可能产生孤立图像，做配对检查时按 stamp 精确匹配。
不要把网络接收时间当成左右曝光时间差。进一步检查可参考
[接收验证脚本](../schurvins/tests/calibration_receiver.py)。

## 5. 转换为 ROS 1 bag

这里采用 Kalibr 常用的 ROS 1 工作流，不能把上述 ROS 2 目录直接作为 ROS 1 `.bag` 传入。
在本机用独立 Python 虚拟环境安装转换工具（不需要启动 ROS 1）：

```sh
python3 -m venv /home/ncer/tidesense-calibration/rosbags-venv
/home/ncer/tidesense-calibration/rosbags-venv/bin/pip install rosbags
/home/ncer/tidesense-calibration/rosbags-venv/bin/rosbags-convert \
  /home/ncer/tidesense-calibration/tidesense-stereo-calib \
  --dst /home/ncer/tidesense-calibration/tidesense-stereo-calib.bag
```

输入完整的 ROS 2 bag 目录（包含 `metadata.yaml`），输出 `.bag` 文件必须尚不存在。
转换保留图像和消息 header 时间戳，不要导出截图或给图像重新打接收时间戳。
工具用法与版本限制见 [Rosbags 官方转换说明](https://ternaris.gitlab.io/rosbags/topics/convert.html)。

## 6. 运行 Kalibr

在**单独的 ROS 1 / Kalibr 环境**执行，不要在已 source ROS 2 的终端里混用两套环境。
以下假设 Kalibr 已安装到 `/home/ncer/kalibr_ws`；若使用容器，请挂载 bag 和本目录，
并将命令中的绝对路径改为容器内路径。

```sh
source /opt/ros/noetic/setup.bash
source /home/ncer/kalibr_ws/devel/setup.bash
cd /home/ncer/tidesense-calibration
rosbag info tidesense-stereo-calib.bag
rosrun kalibr kalibr_calibrate_cameras \
  --bag /home/ncer/tidesense-calibration/tidesense-stereo-calib.bag \
  --topics /tidesense/left/image_raw /tidesense/right/image_raw \
  --models pinhole-radtan pinhole-radtan \
  --target /home/ncer/sophpi/scripts/calibration/tidesense-aprilgrid-a4-6x4-32mm.yaml
```

- `--topics` 顺序固定为左、右，因此输出 `cam0` 是左相机，`cam1` 是右相机。
- `pinhole-radtan` 是普通镜头的起始模型，不是由 OV9281 型号决定的。
  若使用鱼眼镜头，应根据实际镜头与残差评估模型，例如 `pinhole-equi`，不要仅为降低误差盲目换模型。
- 如需检查角点识别，可在上述命令末尾加 `--show-extraction`，需要可用的图形显示环境。
- 原理和参数见 [Kalibr 双目标定说明](https://github.com/ethz-asl/kalibr/wiki/multiple-camera-calibration)。

## 7. 检查标定结果

保留生成的 `*-camchain.yaml`、`*-results-cam.txt`、`*-report-cam.pdf`，以及原始 bag、
实际使用的 target YAML、镜头/翻转/分辨率配置。

1. 查看左右角点是否覆盖画面中央与边缘，是否包含足够倾斜和距离变化。
2. 看重投影误差分布和异常帧，尤其是边缘是否有系统性偏差。
   以亚像素误差作为质量目标，但平均误差小不等于标定一定准确；还要检查覆盖与重复性。
3. `cam1.T_cn_cnm1` 把左相机坐标转换到右相机坐标。
   其平移向量的长度应接近设计基线 **0.050 m**，不应强行改成 0.050；
   明显偏离时优先检查打印尺寸、单位、板面平整度和角点检测。
4. 保留一正一倒输出时，理想相对旋转接近绕光轴 180°，旋转矩阵约 `diag(-1, -1, 1)`；
   若已把图像转正且光轴近似平行，则应接近单位旋转，实际仍有安装误差。
5. 用另一组不同姿态的数据重复标定，比较内参、基线和旋转是否稳定；有条件时检查校正后对应点的纵向误差。
6. Kalibr 输出不能未经适配就当作当前 SchurVINS 配置文件使用：需要核对字段、相机模型、
   外参方向，以及单独获得的相机–IMU 外参。

已使用本机录制的 795 对图像完成 Podman / Kalibr 端到端标定并生成 PDF，详见
[验证记录](kalibr/VALIDATION.md)。这不代表已独立验证打印尺寸或外部测试集精度。
已有上板图像传输测试及当前 WSL 网络环境限制见
[CALIBRATION-VALIDATION.md](../schurvins/CALIBRATION-VALIDATION.md)。
若遇到 Agent/ROS 发现卡住，先按该记录排查主机网络，不要通过修改标定参数解决通信问题。

## 数据与版本管理

将原始 ROS 2 bag 和标定输出保存在 SDK 根目录的 `cali/`，该目录整体忽略；
脚本默认生成的 `*-kalibr-时间/` 目录也会忽略。构建缓存、下载源码和诊断文件位于
已忽略的 `install/`。自定义输出目录建议同样放到 `cali/` 下。

版本库保留标定板 PDF、PNG 预览、target YAML、生成器、采集/标定脚本和验证摘要。
Python 虚拟环境及字节码由本目录 `.gitignore` 忽略。

## 相机–IMU 联合标定

### 联合采集与录包

先完成上述双目标定，保留 `camchain-*.yaml`。确认板上已经安装 SC7U22 IIO/INT1 内核和
新版 micro-ROS 程序包。Agent 和主机 ROS 2 环境设置沿用前文。

板端运行（推荐图像 20 对/秒、IMU 200 Hz）：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --calibrate /mnt/data/sensor_cfg.ini --fps 20 --imu auto \
  --timestamps /tmp/image-stamps.csv --imu-timestamps /tmp/imu-stamps.csv
```

`--imu auto` 启用 IMU，也可指定 `--imu /dev/iio:device0`。不加此参数则保持纯双目模式。
`--fps` 只改变图像发布频率，IMU 固定为 200 Hz。这里不运行 SchurVINS 估计器、不需要加载
双目 YAML，输出的 IMU 也没有姿态融合。图像与 IMU 共用同一时钟映射，保留每条硬中断时间。
相机到达时间仍不是曝光中心，需要通过后续时间标定估计偏移。

主机新终端：

```sh
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
cd /home/ncer/tidesense-calibration
ros2 bag record --storage sqlite3 -o tidesense-imucam \
  /tidesense/left/image_raw /tidesense/right/image_raw /tidesense/imu
```

检查三个话题都出现 `Subscribed to topic`，确认图像清晰、IMU 约 200 Hz 后开始运动。
录制完成先 Ctrl-C 停止板端采集，等 IMU 队列发送结束，再停止录包。若板端报队列满、
通信失败或 IIO 时序错误，本次录制不应作为完整数据使用，修复后重新录制。
详细消息单位、缓冲和错误行为见 [CALIBRATION.md](../schurvins/CALIBRATION.md)。

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

无需 ROS 1 转包或相机消息，直接读取 `/tidesense/imu` 的加速度（m/s²）和角速度（rad/s）。
可指定 `--imu-topic /your/imu`、`--output 新目录`，也可使用
`--skip-seconds 60 --duration 1200` 跳过开始阶段并截取一段时间；时间以第一条 IMU
header 为起点，不使用主机接收时间。不会重采样、插值、降采样或去除线性漂移。

输出 `allan.png`、`allan.pdf`、`allan.csv`、`allan.json`、`allan-report.md` 和 `imu.yaml`，
保留命令、分析脚本与容器信息。使用重叠 Allan 偏差，按 XYZ 分轴拟合：

- 白噪声：`ADEV(τ) = N / sqrt(τ)`，拟合斜率 −1/2，输出噪声密度 N。
- 偏置随机游走：`ADEV(τ) = K * sqrt(τ/3)`，拟合斜率 +1/2，输出 K。
- 曲线最低点/偏置不稳定性不直接当作 Kalibr 的 random_walk。
- 采样间隔偏离中位数超过 20% 时分段，统计窗口不会跨越缺口。
- 拟合至少 5 点、跨度半个十倍区间、10 个不重叠样本对；自由斜率偏差不超过 0.15，
  固定斜率拟合 log10 RMS 不超过 0.06。白噪声搜索 0.02–1 s；随机游走从 1 s 后、
  且在曲线最低点之后搜索。阈值是自动筛选规则，不是统计置信区间。

Kalibr 每种噪声只接收一个标量，脚本在三轴均可拟合时取三轴最大值，不自动乘放大系数。
某轴缺少可靠区间，或检测到明显运动/漂移，该参数写为 `null` 并说明原因；
必须先检查报告，不能将含 `null` 的 YAML 直接用于 `--imu-cam`。
没有温度数据，无法证明温度稳定；“未检测到明显运动”也不是静止状态的严格证明。
只有完整且经检查的 `imu.yaml` 才作为下一步 `--imu` 输入。

本次 `cali/sc7u22-static` 实测约 1310.49 s / 261472 条、199.524 Hz，未发现时间倒退
或超过上述阈值的间隔。结果存于 `cali/sc7u22-static-allan-20260922/`：
陀螺仪白噪声密度约 `1.238e-4 rad/s/√Hz`，加速度计约 `1.339e-3 m/s²/√Hz`。
随机游走未在所有轴找到足够区间，两个 random_walk 保留 null，尚非完整 Kalibr 输入。
此数据的加速度模长均值约 9.1047 m/s²；Allan 分析不会修正零偏或比例系数，
若后续改变标定尺度/滤波配置，需要重新评估噪声参数。

建议传感器预热后，保持板子和温度稳定，延长静止记录以观察长时间漂移。
[Kalibr 官方](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model)建议约 15–24 小时静止数据；
21 分钟可先估计短时间白噪声，但不能保证随机游走估计充分。

#### 使用噪声参数进行联合标定

复制 [sc7u22-imu.yaml.example](sc7u22-imu.yaml.example) 为自己的 `sc7u22-imu.yaml`，
填入实际测量得到的四个噪声参数。模板中的 `null` 是待填写标记，不能直接交给 Kalibr；
没有虚构 SC7U22 的噪声密度和随机游走系数。可通过静止数据的 Allan 分析估计，
定义见 [Kalibr IMU noise model](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model)。

新版 LiveImu 会加载网页保存的六面校准 JSON，修正加速度计的固定零偏/比例后发布；
启动时核对 `six-face accelerometer calibration applied` 日志。历史 bag 不会自动修正。
此前这块板未经修正的静止加速度模长约 9.02 m/s²，明显偏离标准重力。
下面的 `calibrated` 模型要求输入已完成惯性内参校准；
不要把采集正常等同于惯性精度合格。

使用一键入口，无需在主机安装或 source ROS 1；容器环境与双目标定相同：

本机容器已更新相机–IMU 所需的 SciPy 依赖。其他机器若使用旧镜像，先执行
`scripts/calibration/kalibr/build.sh --runtime-only`，无需重编译 Kalibr。

```sh
scripts/calibration/kalibr.sh ~/sophpi/cali/tidesense-imucam \
  --imu-cam \
  --cam ~/sophpi/cali/tidesense-stereo-calib-kalibr-20260922-001523/stereo-camchain.yaml \
  --imu ~/sophpi/cali/sc7u22-imu.yaml
```

替换 `--cam` 为本次双目标定结果、`--imu` 为已填好实测噪声参数的文件。
相机话题、模型和分辨率从 camchain 读取，IMU 话题从 IMU YAML 的 `rostopic` 读取；
联合模式不再传 `--topics` / `--models`。可用 `--target` 指定实际打印的标定板。
双目采集时若一正一倒，此处继续保持原方向，不要修改寄存器旋转或先校正图像。

脚本自动校验并转换 ROS 2 bag，再运行 `kalibr_calibrate_imu_camera --imu-models calibrated`。
转换保留所有 IMU 样本及 header 时间戳；图像仅保留时间戳精确匹配的左右帧对，
bag 索引时间取各自 header 时间，不取网络接收时间。检查分辨率与已有内参是否一致、
IMU 时间戳递增且与图像重叠，拒绝噪声参数为 null 或非正数的模板。
不会自动校正 IMU 零偏/尺度，也不旋转图像或 IMU 坐标轴。

可先在相同命令末尾加 `--convert-only` 检查数据；默认新建输出目录，已有目录拒绝覆盖。
输出包含 `imucam.bag`、`imucam.conversion.json`、`imucam-camchain-imucam.yaml`、
`imucam-results-imucam.txt`、`imucam-report-imucam.pdf`，以及输入 YAML 副本和日志。
`imucam.conversion.json` 中有 IMU 数量、实际平均频率、最大采样间隔和重叠样本数，
这些检查通过不代表运动激励、惯性内参或标定质量合格。

默认启用时间偏移标定，不加 `--no-time-calibration`。检查报告中的重投影误差、惯性残差、
各轴运动激励以及时间戳间隔。输出包括 `imucam-camchain-imucam.yaml`、报告 PDF 和结果文本。
根据 [Kalibr YAML 定义](https://github.com/ethz-asl/kalibr/wiki/yaml-formats)：

- `T_cam_imu` 把 IMU 坐标转换到相机坐标；SchurVINS 的 `T_B_C` 需要其逆矩阵（B 为同一芯片 IMU 坐标系）。
- `timeshift_cam_imu` 满足 `t_imu = t_cam + shift`，单位秒。当前实时入口以左相机 VI PTS 为图像时间，
  对应 cam0 的 shift 转为纳秒后填入 `live_camera_time_offset_ns`，并保持 `delay_imu_cam: 0.0`，避免重复修正。
- 改动相机翻转、采样率、IMU 滤波等配置后，应重新评估空间/时间标定；不直接照搬其他板子的参数。

本功能验证的是联合采集和消息时间戳，尚不代表已经完成这块板的 Kalibr 外参求解。

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

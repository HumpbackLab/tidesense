# TideSense 独立双目/IMU 数据集录制

电脑端命令均在工程根目录执行；板端采集与回放在设备上的程序目录执行。

`tidesense_record` 不链接 SchurVINS、OpenVINS、ROS 或 DDS，不运行特征提取和 VIO。
通过根目录 `tide_cam` 在本进程采集双目，复用 SC7U22 IIO 读取代码，无相机辅助进程或 INI。
双目支持 640×400（默认）、640×480、1280×800 mono8；硬件触发约 20 Hz。
IMU 200 Hz，INT1 内核时间戳，10 次中断触发一次 FSIN。
两颗相机使用自动曝光/增益 ISP 配置，曝光上限默认 0.5 ms（500 µs），可通过设备的 `camera.conf` 调整；不旋转或去畸变图像。
单帧空间降噪 YNR 强度默认 192，锐化和跨帧 TNR 关闭；启动日志报告 YNR 强度。
可用 `OV9281_YNR_STRENGTH=64 scripts/dataset/build.sh` 构建原来的强度，对照时保留实际运行日志。
存储采用可逐字节还原的无损压缩。同一份原始录制用于数据集、标定和 VIO 回放，
首次标定不需要已有相机参数，完整流程见 [标定说明](../calibration/README.md)。

## 编译与部署

TideSense `build_all` 默认构建本程序并预装到 SD 卡的 `/mnt/data/tidesense-dataset/`，可直接进入该目录录制。
单独构建和手动部署：

```sh
scripts/dataset/build.sh
scp -O install/soc_sg2002_tidesense_arm64_sd/tidesense-dataset-a53.tar root@192.168.123.99:/mnt/data/
# 板端
cd /mnt/data
tar xf tidesense-dataset-a53.tar
```

首次构建依赖现有 `scripts/schurvins/build.sh` 建好的 GCC11 OpenCV/yaml-cpp 静态库；
录制程序本身单独编译，不构建/链接 VINS。构建脚本下载并校验
[Zstandard 1.5.7](https://github.com/facebook/zstd/releases/tag/v1.5.7) 的固定 SHA256，静态链接。
构建时同时准备 `tide_cam` 所需 SDK archives，最终程序全静态链接。

## 录制

先停止占用相机或 IIO 的 VIO/标定/PFC 采集程序。SD 必须挂载 async，不能 sync。

```sh
cd /mnt/data/tidesense-dataset
record_dir=/mnt/data/dataset-$(date +%Y%m%d-%H%M%S)
./run.sh --output "$record_dir" >"$record_dir.log" 2>&1
```

前台运行，Ctrl-C 正常停止、排空写队列并同步到 SD。固定时长可加 `--seconds 120`。
输出目录必须不存在。用 `--width 640 --height 480` 等参数选择尺寸，无需 `--sensor-ini` / `--camera-helper`。
`--imu` 默认 auto。`--calibration` 可省略；它仅保存参数和保证 IMU 覆盖时间偏移，录制时不运行估计器。
传入标定时两颗相机的尺寸必须匹配采集设置。实际采集参数保存为 `camera-config.yaml`。

IMU 只做标称 SI 单位换算，不读取外部校准文件，也不应用 VIO 内参。
`imu-source.yaml` 保存 IIO scale、采样率和 `imu_value_domain: raw_si`。
运行或回放 VIO 时，从匹配的算法配置加载单级 IMU 内参并应用一次。

## 写盘与完整性

- 原始图像为 10.24 MB/s（约 614 MB/分钟，十进制）；本板 SD 实测约 9.18 MB/s，无法持续保存未压缩流。
- `frames.bin` 每对图做逐行 uint8 差分 + Zstandard fast（level -100，并显式启用字面量 Huffman 编码）无损压缩，再顺序写入，附带每对校验和。快速模式为 A53 的相机、IMU 和预览保留 CPU 余量；原始像素可逐字节还原。
- 压缩线程和写盘线程独立运行，中间传递拥有独立存储的压缩包；写入阻塞期间仍可继续压缩。字面量 Huffman 编码避免快速档默认关闭该步骤后写入量过大。
- 网页预览在独立线程使用 VPSS 缩放、VENC JPEG 编码并通过 HTTP/MJPEG 直接发送到浏览器。预览只取最新帧，录制仍保存原始尺寸、原方向的无损图像。
- 压缩率取决于图像纹理/噪声。持续压缩或 SD 写入跟不上采集时会明确报错，不静默丢帧。
- 未写入的图像对总容量默认 64，覆盖原始队列、压缩队列和处理中数据，按最坏像素大小约 32.8 MB，另有固定工作缓冲。`--queue-pairs 1..128` 调整这个总容量；相机另有 8 对 FIFO。
- 上述带宽和内存数字对应 640×400。640×480 / 1280×800 原始图像带宽分别为
  12.29 / 40.96 MB/s，队列每对像素分别为 0.614 / 2.048 MB，必须留足 RAM。
  本板 1280×800 连续录制实测压缩和 SD 吞吐不足，16 对写队列会满并报错；
  1 秒、21 对短段可正常排空并完成。增大队列只吸收短暂积压，不保证持续录制。
- 队列满、SD 空间不足、相机参考号/帧序/时间戳跳变、IMU 间隔 >7.5ms 都报错并保留 `INCOMPLETE`。
- 压缩线程先计算压缩包摘要，再逐帧解压并与编码输入逐字节比较；复制及出队时保持同一摘要基准。写入后 flush 并 pread 当前帧，核对实际文件长度和压缩字节；读取通常命中刚写入的缓存，不在停止后扫卡。
- 元数据 `cached_write_verified` 表示逐帧缓存写回检查通过，`expected_frames_xxh64` 为写入前计算的整段压缩归档摘要，供后续定位存储差异使用。
- 停止后排空队列、关闭流并对本次录制的图像、IMU、配置、元数据及目录分别 fsync，然后发布 `COMPLETE`。不在板端重读整段文件，也不对整个文件系统做全局同步。
- `COMPLETE` 表示采集已结束且文件已刷盘；`encoding_verified=true` 表示录制时的编码自检通过，`archive_verified=false` 明确表示存储文件尚未完整回读。取回电脑和标定入口仍严格执行全部帧 checksum、数量及相机 / IMU 连续性检查。
- 元数据保存队列排空时间 `finalize_drain_ms` 和数据文件同步时间 `data_sync_ms`，不能直接拔电。
- 最终日志和 `metadata.yaml` 给出帧数、IMU 数、实际速率、最大间隔和队列峰值。
- 元数据同时保存压缩级别、编码与即时解压校验阶段、图像写入及缓存读回阶段各自的墙钟 / 线程 CPU 耗时，以及原始 / 压缩队列峰值，便于区分 CPU 竞争与存储压力。
- 仅记录相机/IMU 完成启动后的连续区间。IMU 从第一帧前开始，到最后一帧后结束；不包含启动过程和停止后的空闲样本。
- 原始相机时间戳是 VI arrival PTS，**不是曝光时刻**。与 IMU 同属 monotonic 时钟；FSIN 同步不能消除读出延迟，回放仍需使用匹配当前曝光/触发方式的时空标定。

二进制格式：文件头 ASCII `TIDESENSE_ZST_V1\n`，随后重复：4 字节小端压缩块长度，
紧接一个独立带 checksum 的 Zstandard 帧。解压后是 56 字节小端
`<IIIIQQQIIII` CameraHeader（magic/version/width/height/sequence/left_ns/right_ns/left_ref/right_ref/flags/reserved），
紧接左图和右图，各 `width × height` 字节，无行填充。640×400 时每路为 256000 字节。
图像字节是逐行差分：
首列原值，其余 `uint8(pixel[x]-pixel[x-1])`，按行累加模 256 还原。头部不做差分。
数据集元数据为 `tidesense-dataset-v2`。导出工具亦可读取早期 `TIDESENSE_RAW_V1\n` 未压缩格式。
IMU CSV：timestamp_ns,gx,gy,gz,ax,ay,az，单位 rad/s、m/s²。

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
- `mav0/imu0/data.csv`：时间戳、标称 SI 角速度与加速度。
- `stereo.csv`、`imu.csv`、`replay.yaml`：直接供本 SDK SchurVINS `--replay`。
- `dataset.bag`（`--bag`）：ROS1 原始 `sensor_msgs/Image` mono8 和 `sensor_msgs/Imu`，按时间排序。

`--calibration SCHUR.yaml` 可在导出时补充/替换参数；没有参数时可以导出 EuRoC/bag，
但不生成 `replay.yaml`。不提供虚假的 EuRoC sensor.yaml 或 groundtruth；算法配置需使用本板的真实标定。
导出器校验整个录制的尺寸一致、像素长度和标定尺寸；PNG 与 ROS Image 使用实际采集尺寸。

导出默认将标定文件的 `live_camera_time_offset_ns` 加到 cam0 PTS，双目共用该帧时刻；
原始左右 PTS 另外保存到 `frames.csv`。`replay.yaml` 清零时间偏移，避免重复应用。
OpenVINS/VINS-Fusion 配置也须设置与导出一致的偏移（已经平移的 bag 应从 0 开始）。
如需重做 Kalibr，使用 `--time-offset-ns 0` 导出未平移时间戳，避免使用旧标定偏移。
导出的 `dataset.bag` 可直接交给 `scripts/calibration/kalibr.sh`，默认相机话题为
`/cam0/image_raw`、`/cam1/image_raw`，IMU 为 `/imu0`，无需安装 ROS。
旧配置尚未迁移到原始 SI 域时，这种导出仍可生成标定 bag，但不会生成 VIO 回放配置。
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

旧版十列 IMU CSV 导出时，使用最后三列保存的原始 SI 加速度，统一输出未补偿数据；需配套使用原始 SI 标定。


## 后台流水线测试

完成 SDK 依赖准备后，在有 OpenCV、yaml-cpp 开发库的宿主机运行：

```sh
cmake -S scripts/dataset/tests -B install/dataset-writer-tests
cmake --build install/dataset-writer-tests -j4
ctest --test-dir install/dataset-writer-tests --output-on-failure
```

测试通过文件 I/O 故障注入验证：写入阻塞时压缩继续、总容量在压缩队列上同样生效、停止等待排空后才标记完成、
磁盘错误保留 INCOMPLETE，编码结果损坏、写入错误及 fsync 错误不发布 COMPLETE，存储内容损坏由完整读取器检出，且成功和溢出前缀可逐像素还原。测试注入库仅用于宿主机测试，不链接到设备录制器。


网页会话支持授权 HTTP 文件下载，由独立线程发送已完成录制的固定文件清单。
通过 TideConfigurator 取回电脑时，校验、原始 SHA-256 与 bag 写入由其自带标定模块单次遍历完成。
显式剔除损坏相机包的副本另存，时间戳与 IMU 不改，详情见 TideConfigurator 说明；正常录制连续性规则保持严格。

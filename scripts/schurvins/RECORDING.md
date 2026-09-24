# VIO 输入录制与离线回放

用于复现初始化后转动/平移发散等问题。录制默认关闭，在 `--vio` 命令末尾增加
`--record NEW_DIR` 开启；micro-ROS 开关独立，录制无需 Agent。

## 录制

在板端执行。先停止已有 VIO/标定采集，关闭 PFC 实时预览，避免抢占 IIO：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
record_dir=/mnt/data/vio-record-$(date +%Y%m%d-%H%M%S)
export SCHUR_MICRO_ROS=0
export SCHUR_IMU_CALIBRATION=/mnt/data/penguin-flight-console/accelerometer-calibration.json
./run.sh --vio tidesense-20260922-221039.yaml /mnt/data/sensor_cfg.ini \
  auto "$record_dir/live-poses.csv" 0 --record "$record_dir"
```

替换 YAML 为本次实际标定。**不要提前 mkdir 录制目录**；程序创建目录并拒绝覆盖已有目录，
父目录需存在。`0` 表示持续运行，可换成 `600` 录制约 30 秒（实际时间取决于消费帧率）。
Ctrl-C 会停止采集、排空写盘队列并关闭录制。等日志出现 `recording: complete` 后再复制数据。
退出码可能因 VIO 未成功跟踪而非零；数据是否完整以 `COMPLETE` 标记及 `metadata.yaml` 为准。

仍需发布位姿时设置：

```sh
export SCHUR_MICRO_ROS=1
export SCHUR_MICRO_ROS_AGENT=192.168.123.2
export SCHUR_MICRO_ROS_PORT=8888
export SCHUR_MICRO_ROS_DOMAIN=0
```

随后使用上面的同一录制命令。Agent 与订阅步骤见 [README](README.md#实时-vio--micro-ros-位姿发布)。

建议开头静止 3–5 秒建立初始状态，再做触发问题的转动/平移，末尾再静止几秒。
将完整终端日志也保留下来，便于对应失败时刻。不要在发散后立即断电或使用 `kill -9`。

## 文件和时间语义

| 文件 | 内容 |
| --- | --- |
| `left/*.pgm`、`right/*.pgm` | VIO 实际输入的 mono8 灰度原图，不旋转、不校正畸变、不缩放；不是传感器 Bayer RAW |
| `stereo.csv` | `timestamp_ns,left_image,right_image`；图片路径相对于 CSV，时间已加左相机 cam–IMU shift |
| `imu.csv` | `timestamp_ns,gx,gy,gz,ax,ay,az`；实际送入估计器的 IMU，rad/s、m/s²，含重力，保留六面修正后的值 |
| `imu-raw.csv` | 同格式，记录六面修正前的 SI 加速度；陀螺仪和时间戳与 `imu.csv` 相同 |
| `frames.csv` | 每帧处理时间戳、采集序号、左右原始 PTS、左右 time_ref、FSIN flags、IMU batch 数量及最后一个 IMU 时间戳 |
| `calibration.yaml` | 运行时相机/IMU 配置快照，保留原始时间补偿 |
| `replay.yaml` | 回放配置；时间补偿设为零，避免再修正已对齐的 CSV 时间 |
| `imu-calibration.yaml` | LiveImu 实际加载的六面系数、IIO scale、采样率和是否启用修正，不依赖后来被替换的 JSON |
| `sensor_cfg.ini` | 相机 INI 快照 |
| `metadata.yaml` | 录制版本、范围、完整性、已写帧数/IMU 数量、写盘队列水位和错误 |
| `COMPLETE` / `INCOMPLETE` | 是否成功结束录制；异常中断保留 `INCOMPLETE` |
| `live-poses.csv` | 上面命令指定的现场位姿输出，供与 replay 对照；无跟踪时仅有表头 |

时间戳均来自采集的 **CLOCK_MONOTONIC**，不是写文件时间、ROS 接收时间或系统墙钟。
IMU 连续记录到最后一帧之后的首个样本，包含首帧之前的初始化历史，保持插值覆盖。
CSV 用 17 位有效数字保存浮点数，避免回放时引入精度截断。

**录的是 VIO 消费的数据**：相机启动暖机帧和 latest-frame 队列被替换、未进入 VIO 的帧不录。
`frames.csv` 中 sequence 跳号可定位消费帧缺口；不伪装成全帧采集。
两个消费帧之间的 IMU 不因图像跳帧而丢弃；最后一帧之后不再参与估计的 IMU 不保存。
因此这份数据用于复现现场算法输入，并非无条件保存相机的全部 20 Hz 输出。

## 回放

完成录制后，在同一个程序包中运行：

```sh
SCHUR_MICRO_ROS=0 ./run.sh --replay "$record_dir" "$record_dir/replay-poses.csv"
```

回放不启动相机、不打开 IIO、不读取板端六面 JSON，直接使用目录内 `replay.yaml`、
`stereo.csv` 和 `imu.csv`。不要改为 `imu-raw.csv`，否则输入不再与现场一致。
不按墙钟 sleep，按处理速度顺序回放；估计器使用录下的原始时间间隔。
相同构建、相同参数有助于复现，跨编译器/优化选项不承诺逐位一致。

原有 CSV 入口也可用：

```sh
SCHUR_MICRO_ROS=0 ./run.sh "$record_dir/replay.yaml" \
  "$record_dir/stereo.csv" "$record_dir/imu.csv" "$record_dir/replay-poses.csv"
```

异常退出的数据默认拒绝回放。为诊断崩溃/发散，可显式尝试已写完整的索引前缀：

```sh
SCHUR_MICRO_ROS=0 ./run.sh --replay "$record_dir" \
  "$record_dir/replay-partial-poses.csv" --allow-incomplete
```

此选项不修复损坏文件。硬断电、磁盘写满等可能留下不完整的 CSV 行或文件，
回放遇到损坏会报错；先备份再检查。索引仅在该帧的图像、IMU 和诊断行写完后追加。
`COMPLETE` 表示程序正常完成写入，不代表每帧都已强制 fsync 到介质，也不代表 VIO 精度合格。

拷到电脑保存（从电脑运行，替换目录名）：

```sh
scp -O -r root@192.168.123.99:/mnt/data/vio-record-YYYYmmdd-HHMMSS ./
```

程序包中的二进制是 AArch64，不能在 x86 电脑直接运行；目前可在板端离线 replay，
电脑可直接读取 PGM/CSV 做时间同步、图像质量和 IMU 检查。

## 性能与空间

640×400 双目 PGM 每对约 512 KB，20 Hz 约 **10.24 MB/s、614 MB/分钟**。
后台写盘队列默认缓存 **32 对**，图像约占 16.4 MB，按 20 Hz 可缓冲约 1.6 秒。
另有正在写入的一对图像、IMU 和 VIO 自身内存开销；队列按实际积压分配。
可在启动前设置容量（1–128 对）：

```sh
export SCHUR_RECORD_QUEUE_PAIRS=32
```

启动日志及 `metadata.yaml` 的 `queue_capacity` 显示实际容量，`queue_high_water`
显示最大积压。增加容量可吸收短暂写盘卡顿，不能解决持续写入速度不足；
128 对仅图像就约占 65.5 MB，设置时应留足 VIO 内存。
队列满、磁盘剩余不足 32 MiB 或写入失败时，程序报错停止并标记不完整，
不静默丢录制帧，也不让队列无限吃内存。

输出文件系统必须使用 async 挂载（当前 TideSense 已配置），不能录到 sync 挂载的 SD。
建议录制 20–60 秒的短段。启用录制仍会产生内存复制和 SD 带宽负担，应结合
现场控制台的 `replaced_pending` 和 `metadata.yaml` 队列水位判断影响。

## 2026-09-23 验证记录

- GCC 11 A53 全静态构建：带 micro-ROS 和不带 micro-ROS 两种入口均通过。
- 板端 `schurvins_recording_test`：像素一致性、纳秒时间戳、浮点精度、配置快照、
  覆盖保护、写盘错误标记通过；合成录制回放 6/6 帧跟踪和 IMU 融合。
- 真实双目录制 100 对 / 200 张图像，1028 条 IMU，跨度 5.000056 秒；
  现场和 replay 均为 `frames=100 tracking=100 imu_fused=100`。
- 逐项核对原始/已校准 IMU 关系，最大 IMU 间隔 5.07432 ms；写盘队列最高 2/4 对。
  现场与回放的 100 个输出时间戳一致，位姿各分量最大绝对差约 1.87e-9。
- 未完成数据默认拒绝，显式 `--allow-incomplete` 可回放有效前缀；SIGINT 停止会排空队列并生成 COMPLETE。

本轮真实数据位于板端 `/mnt/data/recording-live-20260923`，本机副本和数值核对结果位于
`install/schurvins-gcc11-static/recording-results/recording-live-20260923/verification.json`。
这是录制/回放一致性验证，尚未录下用户描述的运动发散过程，也不代表定位精度验证。

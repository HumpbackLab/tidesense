# SC7U22 实时 IMU + 双目

实时入口将两路 OV9281 640×400 / 20fps 和 SC7U22 六轴 200Hz 数据送入 SchurVINS。
纯视觉 `--camera` / `--camera-smoke`、离线 CSV 和合成自检入口仍可使用。

## 设备运行

```sh
cd /mnt/data/schurvins-gcc11-static
# 独立检查 IMU 频率、数据递增和静止重力模长，测试时保持板子静止
./run.sh --imu-test auto 10
# 真实双目 + IMU 输入测试，使用占位标定，不输出位姿
./run.sh --vio-smoke /mnt/data/sensor_cfg.ini auto 120
# 安装镜头、完成实际双目/惯性标定后运行；0 表示持续运行
./run.sh --vio actual-calibration.yaml /mnt/data/sensor_cfg.ini auto /tmp/vio-poses.csv 0
```

`auto` 按 IIO 名称 `sc7u22` 和物理 SPI2 控制器 `0x041a0000` 查找 `/dev/iio:deviceN`。
也可直接指定 IIO 设备节点。程序不再绑定 spidev 或在用户态访问 IMU 寄存器。
需要安装配套的新内核、设备树和厂商模块；旧内核只有 spidev 时明确报错，不回退到轮询。

## 采集与融合

- 内核 `drivers/iio/imu/sc7u22.c` 驱动拥有 SPI2 和 INT1。
  INT1 接 SG2002 pin 53 / SD1_D1 / PWR_GPIO20，推挽高有效、上升沿中断。
- 硬件固定 **200 Hz**、加速度 ±16g、陀螺仪 ±2000°/s、OSR4。
  使用具名寄存器/位定义构造配置，启动时回读验证；IIO 频率/量程属性只读，防止与 VIO 配置不一致。
- INT1 路由陀螺仪数据就绪。硬中断入口首先调用 `ktime_get_ns()` 保存 **CLOCK_MONOTONIC**
  纳秒时间；线程化中断读取六轴并检查两路数据就绪。用户态不重新打时间戳。
  先启用 GPIO IRQ、再路由传感器 DRDY，避免启动丢边沿。首次路由可能对应启动期间
  保留的数据，该首条中断数据会被丢弃。
- SPI mode 3、1 MHz；保留本板已验证的事务前 WHO_AM_I 预读。
  SPI 消息线程通过内核 `spi->rt` 机制设为实时优先级，避免单 A53 上普通 SPI 工作线程被计算任务饿死。
- FIFO 关闭、BDU 开启；每次中断对应一次六轴寄存器读取。读取耗时达到一个采样周期（5 ms）时
  丢弃并统计，不把可能已更新的数据配上旧时间。没有硬件序号，所以不能声称能精确统计所有丢样。
  无数据时消费者超时报错，没有轮询传感器的兜底路径。
- IIO scan：6 个 little-endian int16，顺序 accel XYZ / gyro XYZ，偏移 12–15 是 padding，
  偏移 16 是 little-endian int64 时间戳，共 24 字节。应用验证 scan 布局、频率、时钟，
  并从 IIO scale 属性换算为 rad/s 和 m/s²，保留芯片轴方向。
- 用户态 `poll()` 等待的是 IIO 文件描述符可读，**不是轮询传感器寄存器**。
  IIO 内核缓冲 512 条；每次图像处理前消费全部未交付 IMU，包含第一条晚于图像的样本。
  用户态历史最多 2000 条；时间倒退、间隔达到 15 ms、溢出、饱和或中断流停滞均报错。
- 正常退出/Ctrl-C 禁用 IIO 缓冲；只有 FSIN 也已关闭时才停止传感器测量。SIGKILL 无法执行清理；异常退出后先确认
  无消费者，再手动 `echo 0 > /sys/bus/iio/devices/iio:deviceN/buffer/enable`，同时通过 `bin/fsin.sh stop` 停止遗留的相机触发，然后重新启动。
- 诊断：`cat /sys/bus/iio/devices/iio:deviceN/diagnostics`。
  包含硬中断数、入队数、SPI 错误、未就绪、超时丢弃、内核缓冲丢弃、间隔异常、
  最大中断线程调度延迟及最大中断到读取完成时间，均为本次驱动加载以来累计值。
  这些是软件可观测指标，不能替代外部仪器测得的中断边沿延迟。

## 标定与时间

### 六面加速度计校准

LiveImu 默认读取网页六面校准保存的
`/mnt/data/penguin-flight-console/accelerometer-calibration.json`。
每次启动读取一次，在 IIO 原始值换算为 m/s² 后执行
`corrected[i] = (raw_si[i] - bias_m_s2[i]) * correction_scale[i]`。
VIO、`--calibrate --imu auto` 的 ROS IMU 发布以及 `--imu-test` 共用此读取层，均生效。
芯片轴方向、陀螺仪与 IRQ 时间戳保持原值；不向内核回写偏移。

程序检查 schema/board/sensor/frame、200 Hz、IIO scale 和有限且有效的 XYZ 参数。
文件缺失/损坏/不匹配会拒绝启动。`SCHUR_IMU_CALIBRATION=/path/to/file.json`
可指定另一份校准；仅在明确需要原始 SI 数据时设置 `SCHUR_IMU_CALIBRATION=none`，
日志会明确标为 DISABLED。正常启动日志打印文件路径、零偏和比例系数。
网页重新校准后需要重启消费者才能加载新参数。

此前采集的 bag 不会自动修改；此前 cam–IMU 试验使用的是未修正 IMU 数据，
不能将其结果视为应用六面校准后的正式标定。后续应对校正后的数据重新标定。
此文件的 bias/scale 是传感器固定修正；VIO 中的 acc_bias 是修正后残余偏置的估计，
不要将同一组六面 bias 再填入初始化参数重复扣除。

数值/元数据回归目标：`schurvins_accel_calibration_test`，可通过
`cmake --build install/schurvins-gcc11-static/core-micro-ros --target schurvins_accel_calibration_test`
交叉构建后上板运行。

2026-09-22 接入验证：使用板端已保存的 JSON，回归测试通过；独立读取各 5 秒，
修正后加速度均值约 `[-0.5803, 0.4275, 9.9582] m/s²`、模长 9.9842；
显式关闭修正时约 `[-1.0486, 0.5884, 9.2355] m/s²`、模长 9.3134。
两次为顺序采样，不能视为逐样本对照，但与固定修正公式一致。
采样率约 199.53 Hz，无丢弃和异常间隔。修正后的重力模长仍有残差，
校准参数的最终精度需另行复核。
VIO 入口验证确认加载修正、100 帧全部有 IMU 覆盖；当时场景三角化不足，tracking=0，
该次不是成功定位测试。micro-ROS 保持关闭，测试结束后没有后台运行进程。

`--vio` 要求真实 640×400 双目标定及 `imu_params` / `imu_initialization`，其中 `imu_rate: 200`。
`T_B_C` 的 B 必须是上述芯片 IMU 坐标系，不能把原来相机坐标系的双目标定直接当成惯性外参。
需要实际测定噪声、偏置、相机—IMU 外参与时间偏移。

左 VI PTS 和 IMU 都使用单调时钟，但 VI PTS 是图像到达时间，IMU 是硬中断响应时间。
INT 时间不包含用户态调度和 SPI 读取延迟，但仍存在 GPIO/CPU 中断响应延迟和传感器内部滤波延迟。
这不等于曝光中心和惯性物理测量时刻已精确对齐；需要重新标定相机–IMU 时间偏移。
`--calibrate` 默认只发图像；增加 `--imu auto` 后同时发布 200 Hz `/tidesense/imu`，使用本驱动的 IRQ 时间戳。详见 [联合标定说明](CALIBRATION.md)。
可在 YAML 顶层填入已标定的 `live_camera_time_offset_ns`（有符号整数）：

```yaml
# t_image_for_estimator = left_vi_pts_ns + live_camera_time_offset_ns
live_camera_time_offset_ns: 0
imu_params:
  delay_imu_cam: 0.0
  imu_rate: 200
  # 其余参数使用完整的实际标定文件
```

默认偏移 0 不代表已经同步标定。入口要求 `delay_imu_cam=0`，避免重复应用时间修正。
FSIN 只同步两颗相机，当前没有相机与 IMU 的硬件共同触发。
`--vio-smoke` 使用 `camera-smoke.yaml` 占位参数，只验证输入和融合调用链；未装镜头时不能验证 VIO 精度。

## 构建、部署与回退

在 SDK 根目录：

```sh
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
build_kernel
clean_osdrv
build_osdrv
SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh
```

需配套更新 SD 第一分区的 `boot.sd` 和根文件系统 `/mnt/system/ko/` 下的模块，
再重启。**SD 第一分区必须复制 `install/soc_sg2002_tidesense_arm64_sd/rawimages/boot.sd`**
（FIT 魔数 `d0 0d fe ed`），不能复制上一级带 `CIMG` 升级头的同名文件，否则 U-Boot 无法启动。
仅替换应用无法提供 IIO 中断时间戳。SchurVINS 的 ON/OFF micro-ROS 版本都使用 IIO。
本次板端旧启动镜像与旧模块备份在 `/mnt/data/sc7u22-iio-backup/boot.sd.before-iio`
和 `/mnt/data/sc7u22-iio-backup/ko.before-iio.tar`。
回退时将旧 `boot.sd` 复制回挂载的 SD 第一分区，恢复匹配的模块，再同步并重启。
旧版用户态 spidev 程序只适用于旧内核；不要在 IIO 驱动运行时强制解绑去运行旧程序。

寄存器依据：[士兰微 SC7U22TH 说明书 1.0](https://docs.gtai-tech.com/static-file/a72e4498-eb87-4ec6-8b93-e7b5dff71aee/4be0b73b-7008-472b-9730-a0415d090127.pdf)
5.9、5.40、5.42、5.48；初始化协议的 PX4 授权见 `LICENSE.PX4-SC7U22`。

## IIO / INT1 实板验证（2026-09-20）

新内核、设备树、厂商模块与两种 SchurVINS 程序已部署到当前 TideSense。
配置为 200 Hz，实际速率约 199.53 Hz；没有人为按 5 ms 补造时间戳。

| 测试 | 结果 |
| --- | --- |
| 连续启停 | 20 次，每次 1 秒，全部通过 |
| 独立 IMU 约 10 秒 | 2004 条，199.534 Hz；间隔 4.995–5.028 ms |
| 双目＋IMU | 120 对全部融合；12020 条 IMU，199.531 Hz，最大间隔 5.079 ms |
| 每帧额外处理延迟 100 ms | 40 对全部融合；5832 条 IMU，199.528 Hz，最大间隔 5.076 ms |
| Ctrl-C | 正常退出，IIO buffer/enable=0、FSIN enable=0 |
| 互斥与错误输入 | 第二个 IIO 打开被内核 EBUSY 拒绝；缺失 IMU、占位生产标定被拒绝 |
| micro-ROS ON / OFF | A53 GCC11 全静态构建通过；两种二进制 QEMU 合成自检均为 6 帧融合通过 |

最终累计 24313 次硬中断、24289 条入队；差值 24 为每次启动主动丢弃的第一条数据。
SPI 错误、未就绪、超时读取丢弃、内核缓冲丢弃、异常间隔计数均为 0。
最大硬中断到线程开始延迟 3.086 ms，最大硬中断到读取完成 4.513 ms。
时间戳取自此前的硬中断，不取这些延迟之后的读取完成时间。
独立测试中用户态最大接收滞后约 40.6 ms，但记录的采样间隔仍约 5 ms；
这说明缓冲消费延迟没有被写成采样时间。以上均为本次测试观测值，不保证最坏实时延迟。

修复并验证了两项启用问题：板级 GPIO 缺少 `#interrupt-cells`；
先路由 DRDY、后启用 IRQ 可能丢掉首次上升沿。最终顺序是先启用 IRQ 再路由 DRDY。

静止加速度模长约 **9.02 m/s²**，偏离标准重力；仍需校准尺度/零偏并检查传感器精度，
不能把读取和时序测试通过解释为惯性测量准确。VIO 测试使用占位标定，不验证位姿精度。
相机 VI 到达时刻与 IMU 数据就绪时刻之间的偏移仍需 cam–IMU 标定；
标定模式可用 `--imu auto` 录制图像和 IMU，之后通过 Kalibr 求解外参与时间偏移。

原始记录：主机 `install/sc7u22-iio/board-restarts.log`、
`install/sc7u22-iio/results/sc7u22-iio-acceptance/`；
板端 `/mnt/data/sc7u22-iio-acceptance/`。

## 历史实板验证（2026-09-18，旧 spidev 轮询实现）

以下数据不适用于本次 IIO/INT1 实现；新实现的中断时序需要单独验证。

完整回归脚本：`./bin/test-imu-board.sh "$PWD" /mnt/data/sensor_cfg.ini`。
默认日志目录 `/tmp/schurvins-imu-test`；测试会使用两路相机与 IMU，结束后停止采集。

| 测试 | 最终结果 |
| --- | --- |
| 独立 IMU 10 秒 | 2005 条，200.48Hz，最大间隔 5.44ms，静止重力模长 10.04m/s² |
| 双目＋IMU | 120 对均 `imu_fused`，加入 1384 条 IMU；200.47Hz，最大间隔 9.06ms |
| 额外 100ms 图像处理延迟 | 处理 40 对、替换 88 对旧图像，加入 1224 条 IMU，软件惯性队列未丢样 |
| 纯视觉 / 离线合成自检 | 分别 30 对无 IMU、6 对均 IMU 融合，通过 |
| 互斥、错误标定、缺失 IMU | 拒绝并返回非零，通过 |
| Ctrl-C 清理 | 58 对均融合后正常退出；最终 FSIN 关闭，SPI 线程恢复 SCHED_OTHER/0 |

并发采集仍有软件时间戳抖动（最终 120 对测试有 8 个间隔超过 7.5ms），不等于硬件同步精度。
未装镜头、使用占位标定；以上仅验证数据接入、融合调用和清理，不验证位姿精度。
测试摘要见 `board-test-imu.log`，原始日志保存在主机
`install/schurvins-gcc11-static/imu-results/schur-imu-acceptance/`。
部署前设备程序备份在主机 `install/schurvins-gcc11-static/backup-before-imu-20260918/board-app.tar`。

## INT1 分频触发相机

当前内核支持 PWR_GPIO0 FSIN：开启后每 10 次 INT1 硬中断触发一次（约 20 Hz），
上升沿在读取 SPI 之前直接写 GPIO，100 µs 后由硬高精度定时器拉低。
启用 FSIN 会自动维持 200 Hz IMU，即使没有 IIO 消费者也会读取并清除 DRDY。
相机先启动、IIO 后启动不会重置分频；相机停止后已有 IIO 消费者仍能继续读取。
接口与软件延迟统计见 [OV9281 说明](../ov9281/README.md)。

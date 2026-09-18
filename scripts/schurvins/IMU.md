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

`auto` 根据 SC7U22 modalias 和物理 SPI2 控制器 `0x041a0000` 查找设备，绑定 spidev；
当前内核对应 `/dev/spidev0.0`。也可以指定已绑定的设备节点。
不会解绑其它驱动，也没有添加开机服务。先停止旧 `scripts/sc7u22` 测试程序；
新入口之间通过设备文件锁互斥，旧诊断程序不使用此锁。

## 采集与融合

- SPI mode 3、1MHz；初始化复用已有诊断的协议，识别 WHO_AM_I=0x6a 后才配置。
  加速度 ±16g、陀螺仪 ±2000°/s，ACC_CONF/GYR_CONF 均为 `0x89`：硬件 200Hz，OSR4 平均滤波。
  全部关键寄存器回读校验。板上空闲后的第一次 SPI 事务曾返回零，因此每次操作前增加
  无副作用的 WHO_AM_I 预读；写寄存器后等待 1ms。未更改用户的原诊断程序。
- 独立线程以 0.5ms 周期检查两路 DRDY，仅数据就绪时读取 12 字节六轴数据。
  BDU/地址自增开启，FIFO 关闭。不会用重复的旧寄存器值拼凑 200Hz，也不循环清空 FIFO。
  线程尝试 `SCHED_FIFO/10`；在当前单核板上还将对应 SPI2 内核工作线程临时设为
  `SCHED_FIFO/11`，退出时恢复其原调度策略/优先级（不会降低已有的更高实时优先级）。
  调度设置失败会记录日志；过大时序间隔仍然报错退出。
- 输出角速度为 rad/s、加速度为 m/s²，保留芯片 XYZ 轴方向。
  每个样本用 SPI 读取前后 `CLOCK_MONOTONIC` 的中点时间，**不是硬件锁存采样时间**。
  存在滤波延迟、轮询/调度抖动；没有伪造严格等间隔的时间戳。
- 图像队列只留最新一对；IMU 队列保留全部样本。每次处理图像前，将尚未交付的 IMU
  按顺序加入 `ImuHandler`，包含第一条晚于图像时刻的样本，为预积分/插值提供两侧数据。
  因此 IMU 样本是 200Hz，估计器随约 20Hz 图像批量消费；跳过图像不会跳过期间的 IMU。
- IMU 队列最多 2000 条（约 10 秒）；积压溢出、时间倒退、间隔达到 15ms、
  数据就绪停滞、饱和、寄存器错误均终止运行，不静默覆盖惯性历史。
  间隔超过 7.5ms 记入 `imu_late_intervals`；这是时序异常统计，不是硬件丢样计数。
  直接寄存器采集没有硬件序号，`imu_discarded=0` 仅表示软件队列未丢弃样本。
- Ctrl-C/正常结束会停止相机进程/FSIN，结束 IMU 线程并关闭传感器测量。
  相机 VI 初始化/销毁期间可能长时间占用内核；因此先等待相机产出，再开始 IMU
  采样并积累至少 100ms 历史，最后才送图像给估计器。结束时先停止 IMU，再销毁 VI。
  停止后晚于最后图像的尾部 IMU 不再交给估计器；`imu_samples` 与 `imu_delivered` 可因此不同。

## 标定与时间

`--vio` 要求真实 640×400 双目标定及 `imu_params` / `imu_initialization`，其中 `imu_rate: 200`。
`T_B_C` 的 B 必须是上述芯片 IMU 坐标系，不能把原来相机坐标系的双目标定直接当成惯性外参。
需要实际测定噪声、偏置、相机—IMU 外参与时间偏移。

左 VI PTS 和 IMU 都使用单调时钟，但 VI PTS 是图像到达时间，IMU 是寄存器读取时间。
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

寄存器依据：[士兰微 SC7U22TH 说明书 1.0](https://docs.gtai-tech.com/static-file/a72e4498-eb87-4ec6-8b93-e7b5dff71aee/4be0b73b-7008-472b-9730-a0415d090127.pdf)
5.9、5.40、5.42、5.48；初始化协议的 PX4 授权见 `LICENSE.PX4-SC7U22`。

## 实板验证（2026-09-18）

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

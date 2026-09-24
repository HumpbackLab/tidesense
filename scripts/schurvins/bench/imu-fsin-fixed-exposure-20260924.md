# IMU 分频 FSIN 与固定曝光上板验证

日期：2026-09-24。SC7U22 200 Hz，INT1/PWR_GPIO20；FSIN/PWR_GPIO0。
内核：`5.10.4-tag--00025-g624bff021a42-dirty #9`，含本次中断分频触发修改。
板上 micro-ROS/非 micro-ROS 两个安装目录均已更新 camera helper 与 fsin.sh。

## 结果

| 项目 | 结果 |
| --- | --- |
| 有限 20 脉冲 | 最后触发 IRQ 计数 200，GPIO 拉高次数 20；之后不再输出 |
| 相机单独采集 | 120 帧，19.954 fps，配对丢弃 0，IIO buffer 未启用 |
| 相机 + IMU | 400 个处理帧全部覆盖 IMU；相机收到 403 帧（启动跳过 3），19.954 fps |
| 联合 IMU | 199.538 Hz；4018 样本，4016 交付；最大间隔 5.05456 ms，late/discarded=0 |
| 独立 IMU | 998 样本，199.537 Hz，通过；没有增加 FSIN 脉冲 |
| IIO 启停独立性 | FSIN 开启后启动/停止 IMU 测试，IIO buffer=0 时 FSIN 仍 enable=1，已输出 101 脉冲；随后 stop 正常 |
| 曝光 | 两路多次 ISP 查询均 1996 µs（目标 2000 µs，按行量化），AGain/DGain/ISPDGain 均 Q10=1024 |
| IRQ 入口至 GPIO 写入 | 各测试最大 3.32–4.60 µs；这是软件时间戳差，不是电气 IRQ 边沿至 FSIN 的实测 |
| 高电平 | 目标 100 µs，观测最大 112.16 µs；hrtimer 调度有延迟，overruns=0 |
| 最终状态 | FSIN=0、IIO buffer=0，GPIO0 数据位=0，GPIO 复用值=0；没有留下采集进程 |

IMU 驱动累计 SPI errors/not_ready/late_reads/buffer_drops 均为 0，累计 `gaps=1`。
该累计间隔异常的具体发生阶段未记录，不能宣称全程零间隔异常；联合测试的用户态数据没有超限间隔。
测试使用 smoke 占位标定，tracking=0；结果只证明输入链路工作，不证明 VIO 精度或快速旋转稳定性。

日志：`install/tidesense-imu-fsin-validation/`。
旧内核和程序备份：板端 `/mnt/data/backup-before-imu-fsin-20260924/`。
新 SD 启动 FIT：`install/soc_sg2002_tidesense_arm64_sd/rawimages/boot.sd`。
该轮验证未重新生成整卡镜像。

VI PTS 仍为图像到达时间；启用 IMU 分频与固定曝光后需要重新核实相机–IMU 时间偏移。
上述曝光测试使用初始固定增益 1×；随后为改善室内亮度，当前配置改为固定模拟增益 4×，数字/ISP 增益仍为 1×。表中的 1× 查询结果仅对应初始测试，不代表当前增益配置。

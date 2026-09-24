# TideSense OV9281 模式与上板验证

默认是 **640×400 RAW10，标称 20fps，全视场子采样**。保留原来的三个 60fps 配置：

| `sensor_cfg.ini` 中的 `name` | 输出 | 方式 |
| --- | --- | --- |
| `OV_OV9281_MIPI_400P_20FPS_10BIT` | 640×400 @20fps | 全阵列窗口子采样，默认 |
| `OV_OV9281_MIPI_400P_60FPS_10BIT` | 640×400 @60fps | 全阵列窗口子采样 |
| `OV_OV9281_MIPI_480P_60FPS_10BIT` | 640×480 | 居中裁切，原画面坐标 (320,160) 起始 |
| `OV_OV9281_MIPI_800P_60FPS_10BIT` | 1280×800 | 全分辨率 |

左右两路均在 `/mnt/data/sensor_cfg.ini` 配置。左侧地址 0x10、2 lane；右侧地址 0x60、1 lane。
自由运行时，四个配置均使用 HTS=1456；20fps 的 VTS=2730，原有 60fps 的 VTS=910。降帧率在传感器端完成，不是采集后丢帧。ISP/RX 尺寸与传感器输出保持一致。修正 sensor 聚合共享库 `Makefile_full` 对目标文件的依赖，确保增量构建同步更新 `libsns_full.so`。20fps 配置的 AE 默认最大曝光为 2705 行，支持降到 10fps（VTS=5460）；60fps 配置支持降到 30fps（VTS=1820）。最大曝光随 VTS 调整。

640×400 使用 [ArduCAM 参考驱动](https://github.com/ArduCAM/ov9281_driver/blob/main/ov9281.c) 的全阵列子采样寄存器组，调整 HTS/VTS 以保持当前板级时序。它采用参考驱动的水平 binning、垂直子采样设置，不宣称已验证为双方向平均的纯 2×2 binning。裁切寄存器参考 SDK `docs/OV9281_DataSheet.md` 第 4.2、7.8 节。

## 构建与运行

在 SDK 根目录执行：

```sh
scripts/ov9281/build.sh
```

输出 `install/ov9281-test/capture`、`mode_check` 和 `fsin.sh`。脚本编译当前板的 sensor component、sample common，以及测试程序；后者静态链接 SDK 库、动态链接系统 C 运行库，无需在设备额外安装整套 MPI 动态库。

设备上已部署：

- `/mnt/data/sensor_cfg.ini`：默认 640×400、20fps，`hw_sync=1`，等待 FSIN 触发。
- `/mnt/data/ov9281-update/fsin.sh`：PWR_GPIO0 内核 IMU 中断分频触发工具。
- `/mnt/data/ov9281-update/capture`：实际双路 VI 取帧工具。
- `/mnt/data/ov9281-update/mode_check`：真实驱动回调测试，无传感器 I/O。
- `/mnt/data/ov9281-update/libsns_ov9281.so`、`libsns_full.so`：新动态库。
- `/mnt/data/ov9281-update/backup-before-modes/`：原来的 INI 和 OV9281 动态库。
- `/mnt/data/ov9281-update/backup-before-20fps/`：改为 20fps 前的默认 INI、采集工具及两个驱动库。

运行（确认没有其他采集程序占用相机）：

```sh
mkdir -p /tmp/ov9281-test
cd /tmp/ov9281-test
/mnt/data/ov9281-update/mode_check
/mnt/data/ov9281-update/capture /mnt/data/sensor_cfg.ini --fsin-test
```

`capture` 默认读取 `/mnt/data/sensor_cfg.ini`；也可显式指定配置文件。读取每路 600 帧，检查尺寸与时间戳递增，回读输出尺寸/时序/lane 寄存器，打印 VI/MIPI 状态，并在当前目录保存两张灰度 PGM。SDK CV181x 取帧使用 `CVI_VI_GetChnFrame(0, channel)`，其中 channel=0/1。

切换测试模式无需修改默认文件：

```sh
sed s/20FPS/60FPS/g /mnt/data/sensor_cfg.ini > /tmp/sensor-400-60.ini
/mnt/data/ov9281-update/capture /tmp/sensor-400-60.ini --free-run
sed s/400P_20FPS/480P_60FPS/g /mnt/data/sensor_cfg.ini > /tmp/sensor-480.ini
/mnt/data/ov9281-update/capture /tmp/sensor-480.ini --free-run
sed s/400P_20FPS/800P_60FPS/g /mnt/data/sensor_cfg.ini > /tmp/sensor-800.ini
/mnt/data/ov9281-update/capture /tmp/sensor-800.ini --free-run
```

增加 `--free-run` 可仅对本次测试关闭 `hw_sync`，不改写 INI。程序退出后停止 VI/ISP；没有新增开机常驻采集服务。

## 640×400 / 20fps 默认配置验证

新增配置名 `OV_OV9281_MIPI_400P_20FPS_10BIT`。传感器默认模式、板级 INI、ISP 帧率和 AE 帧周期均设为 20fps；镜像脚本继续打包该板级 INI。寄存器组在开流前写入所选模式的 VTS，即使 ISP 尚未产生同步寄存器也使用正确的帧周期。

2026-09-18，上板左右各连续采集 600 帧，实测均约 **20.125fps**，输出均为 640×400，HTS=1456、VTS=2730。这是传感器端降低帧率，未用 VI 输出限速或软件丢帧模拟。20fps 为标称值，实测约高 0.63%；原 60fps 模式也有相同比例偏差。

同一程序回归 640×400 / 60fps，左右各 600 帧均约 60.376fps，VTS=910。默认配置复测及 60fps 回归的两路 MIPI 错误计数均为 0。部署文件已核对 SHA256，日志见 `board-test-20fps.log`。驱动回调测试覆盖全部四个配置，以及 20→10fps、60→30fps 的帧长和曝光上限。

本次未运行真实 VIO。50ms/帧的处理耗时已经接近 20Hz 周期，降采集帧率不等于保证端到端实时性；将来接入实时输入时仍需限制待处理双目帧对队列，保留对应时间段的完整 IMU 数据。

## 2026-09-18 原有 60fps 配置上板结果

设备：192.168.123.99，SG2002 A53，Linux 5.10.4。设备时钟没有校时，日志中的 1970 时间不可作为测试日期。

| 模式 | 每路帧数 | 左/右应用取帧率（VI PTS） | 输出尺寸 |
| --- | --- | --- | --- |
| 640×400 @60fps（原默认） | 600 | 约 60.38 / 60.38fps | 两路正确 |
| 640×480 | 600 | 约 56.19 / 56.19fps | 两路正确 |
| 1280×800 | 600 | 约 60.38 / 60.38fps | 两路正确 |

详情见 `board-test.log`。尺寸回读匹配，数据不是常量缓冲区。模式切换测试中曾观测到单次 WordCount/ECC 错误，检查发现原驱动退出时只关闭 I²C，没有停止传感器输出；现已补上退出停流，避免 RX 重配时传感器继续发送旧模式的数据。最终三个模式依次切换复测时，MIPI ECC/CRC/Header/WordCount/FIFO 错误均为 0，VI LostFrame/VbFail 均为 0；各路连续取帧 600 帧。

640×480 的 60fps 是配置目标，当前双路采集实测未达到。该模式左 VI 计数约 56fps、右 VI 约 61fps；测试程序顺序取左右帧，因此右应用取帧率受左侧限制。自由运行测试也出现同样现象，原因尚未确定，不视为两颗传感器都只能输出 56fps。

驱动回调测试覆盖：20fps 默认模式、四种配置选择、RX/ISP 尺寸一致、设置 WDR_NONE 不覆盖已选模式、20/10fps 及 60/30fps 曝光上限，以及非法尺寸/帧率（包括 NaN）拒绝。

限制：

- 用户确认未装镜头。本次只验数据链路，未验证视场、清晰度、双目标定或 VIO 轨迹效果。
- 上述历史结果来自原驱动，当时 `hw_sync=1` 并非真正的外部单帧触发。新驱动语义及验证见下文。VI PTS 是到达时间，不能直接证明曝光同步精度。
- 更换模式需要匹配对应的相机内参/矫正映射；640×480 是裁切，不能当作 1280×800 等比缩小。装镜头后需完成双目和 IMU-相机标定。
- SDK 日志有既有的 `devmem: not found` 和退出时 AF 未初始化消息；本次尺寸和取帧验证通过，并未把这些日志认定为已修复。

本次无需更换内核，也未重制整卡镜像。板级镜像脚本现会把当前板级 INI 放入 DATA 分区；已有静态链接相机应用需要重新编译链接，单独替换 `.so` 不会更新其内嵌驱动。

## PWR_GPIO0 驱动双目 FSIN

两颗相机共用 SG2002 的 PWR_GPIO0。当前由 SC7U22 内核驱动在每第 10 次 INT1 硬中断中直接拉高 GPIO，200 Hz IMU 对应约 **20 Hz FSIN**；高精度硬定时器约 **100 µs** 后拉低，不在中断中忙等，也不经过用户态或 SPI 读数线程。GPIO 不再复用为 PWM8。采集程序先完成两路初始化，再启用触发，退出前停触发。未启用 IIO buffer 时，驱动仍读取 IMU 以清除 DRDY；启停 IIO buffer 不重置正在工作的 FSIN 分频。

`hw_sync=0` 自由运行，20fps 模式仍使用 VTS=2730。`hw_sync=1` 现在启用外部快照：FSIN 上升沿触发一帧，`0x303f=1`、`0x4f00=1`、`0x0100=0` 等待触发；不再只设置 `0x3823`。无脉冲不出图。参考本仓库 `docs/OV9281_DataSheet.md` §4.8.3，以及 [ArduCAM 外部触发示例](https://github.com/ArduCAM/MIPI_Camera/blob/master/RPI/ov9281_external_trigger.c)。standby/exit 清除唤醒使能，restart 恢复等待触发。

快照模式需要“唤醒 + 曝光 + 读出”时间。实测沿用 VTS=2730 会在 20Hz 下漏掉隔一个脉冲，因此触发模式固定内部 VTS=910，并将曝光上限限定为 **546 行，约 10ms**。对 ISP 报告的标称帧周期仍为 20fps，曝光行时间不变。真实输出频率由 FSIN 决定；工具限制 1–20Hz，**目前上板验证的是 640×400 / 20Hz**，不得将其他配置名中的 60fps 当成已经支持 60Hz 快照。低于 10Hz 或长时间闲置需注意手册的 VN2 保持要求，并丢弃每个新 burst 的首帧。

设备使用：

```sh
# 完整自测：自动预热、有限脉冲计数、停触发检查并退出
cd /tmp/ov9281-test
/mnt/data/ov9281-update/capture /mnt/data/sensor_cfg.ini --fsin-test

# 曝光上限压力测试：请求 100ms，检查驱动限制到 546 行
OV9281_FSIN_MAX_EXPOSURE=1 /mnt/data/ov9281-update/capture /mnt/data/sensor_cfg.ini --fsin-test

# 自己的应用：prepare -> 初始化两路 hw_sync=1 -> start -> 取帧
/mnt/data/ov9281-update/fsin.sh prepare
# 在此初始化自己的相机采集程序
/mnt/data/ov9281-update/fsin.sh start 20
/mnt/data/ov9281-update/fsin.sh status
# 停止采集前执行；恢复 GPIO 输出低电平
/mnt/data/ov9281-update/fsin.sh stop
```

`train 20 604` 输出有限 604 个脉冲；`status` 的 `pulses` 是内核实际执行的 GPIO 拉高次数。达到数量后保持低电平，但仍需 `stop` 释放触发状态和 IMU 采样需求。仅支持 20 Hz（固定 10 分频）；标定发布的较低帧率仍由程序抽帧实现。脚本互斥锁防止并发配置；GPIO 由内核独占，不再导出 GPIO/PWM。PWR_GPIO0 不应接另一个主动驱动源。

需要同时安装新内核、板级 DT 和新版 `fsin.sh`，旧内核缺少接口时脚本明确失败，不回退到独立 PWM。DT 的 `fsin-gpios` 指定 PWR_GPIO0，`silan,fsin-pinmux` 配置该 SDK 的焊盘复用寄存器。原参考 DT 的显示 GPIO 属性已删除。整卡构建会包含新版脚本。

`/sys/bus/iio/devices/iio:deviceN/fsin_diagnostics` 提供脉冲数、最后一次触发的 IRQ 编号/单调时钟时间戳、最大 IRQ 入口至 GPIO 写入耗时、高电平耗时及过期计数。统计的是软件路径，不包含 INT 引脚边沿到进入中断的延迟；电气时序需示波器核对。`diagnostics` 中的 IMU gaps/late_reads 也应一起检查，因为丢失 DRDY 会延长触发周期。

以下为旧 PWM 实现的历史测试数据，不代表当前中断分频实现：

2026-09-18 实测：预热处理 VI/ISP 启动时约 30 帧不交付的行为后，硬件发送 604 个脉冲，两路各取到 604 帧，丢弃前 4 帧后统计 600 帧，两路均 **20.000fps**。触发前、停止预热后、有限脉冲结束后均没有新帧。MIPI ECC/CRC/Header/WordCount/FIFO 错误和 VI LostFrame/VbFail 均为 0。日志见 `board-test-fsin.log`。

这验证了共同触发和一脉冲一帧的链路行为，未用示波器测量引脚波形或曝光起点偏差。两路独立 AE 会选择不同曝光时间；VIO 正式使用时还需统一曝光策略。左右 VI 到达 PTS 仍有约 2.6ms 偏差（左右 lane 数和处理路径不同），不能据此认定实际曝光相差 2.6ms。相机仍未装镜头，未验证成像或 VIO 精度。

测试程序在通道启用前设置 3 帧的有限 VI 队列：SDK 的默认深度为 0，左右顺序取帧时会因调度延迟漏交付；本 SDK 的 `SetChnAttr` 在初始化后再次调用会追加通道而非更新原通道，因此测试通过链接包装在首次配置时设置深度。VIO 应用也应在启用通道前配置有限缓冲，并按时间戳配对，不能假设每次顺序 GetFrame 就一定对应同一个触发。

最终回归还覆盖了请求 100ms 曝光并限制到 546 行的压力测试，以及 `hw_sync=0` 的 640×400/20fps 自由运行：均每路连续 600 帧通过，触发模式为 20.000fps，自由运行为 20.125fps。MIPI 错误计数均为 0。

保留的 640×400/60fps 自由运行配置也完成每路 600 帧回归，实测左 60.375fps、右 60.376fps，MIPI 错误计数均为 0。部署文件 SHA256 与主机一致；测试结束后 PWM 关闭，PWR_GPIO0 回到 GPIO 低电平。更新前文件保存在设备 `backup-before-fsin/`。

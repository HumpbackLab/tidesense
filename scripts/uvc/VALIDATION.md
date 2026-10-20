# TideSense UVC 第一版验证记录

日期：2026-10-20。SG2002 A53，双 OV9281，SC7U22；Windows USB 主机，WSL2 开发环境。

## 最终组合

- 内核 `5.10.4-tag--00026-gb4f72fbbb23a-dirty`，启动标识 #17。
- 匹配的 `dwc2.ko` 使用 `gadget_desc_dma=0`，SHA256：
  `5957bc88edadb4d11d10ca3023b625cd0d42b70406010c37a6cd0d508389215b`。
- USB VID/PID 保持 `3346:1009`，RNDIS OS 子兼容标识统一为 `5162001`。
  Windows 同时识别 Remote NDIS Compatible Device 和 UVC Camera，均正常。
- UVC 1.0，HS-only MJPEG 1280×400@40；每 250µs 最多 3072 字节，JPEG 默认质量 60。
  UVC 预排队 32 个请求，帧间维持空 USB 事务；关流后停止这些事务。
- IMU 400Hz，每十次硬中断触发一次双目公共 FSIN；图像只记录左右 VI PTS，未进行曝光补偿。

## 验证结果

| 项目 | 结果 |
| --- | --- |
| A53 应用交叉编译 | GCC 6.3.1，`-Wall -Wextra -Werror` 通过 |
| 内核、模块及 osdrv | 编译通过；MEDIA_SUPPORT / VIDEO_DEV / VIDEO_V4L2 / USB_F_UVC / USB_CONFIGFS_F_UVC 均为 y；UVC SG 关闭 |
| 协议及输出循环测试 | 6 项通过：C/Python 互通、有符号六轴、APP 内 FF D9、坏长度/截断、重复帧、SDK 非标准 ioctl 成功返回及关流竞态 |
| IMU 提交语义 | C 断言通过：peek 不消费，仅成功提交后移除，失败 QBUF 不丢样本 |
| SDK 应用安装/构建顺序 | 9 项通过，包含 UVC 安装 |
| Shell 静态检查 | run.sh / build.sh 的 shellcheck 通过 |
| 最终版本 UVC 原始收流 | 1000 帧，全部 APP15 解析成功，全部 Pillow 完整解码为 1280×400 RGB |
| 并行调试 | 收流期间 SSH 持续读取板端状态，无中断或超时 |
| 主机关流后再次开流 | 同一采集进程重新接收 100 帧，全部元数据解析成功 |
| UVC stop | 恢复纯 RNDIS，SSH 自动恢复；IMU 恢复 200Hz，buffer=0，FSIN=0 |
| 200Hz 回归 | 300 条连续样本，实测 203.919616Hz，最大间隔 4.91464ms |
| FSIN 限次回归 | 200Hz 下恰好输出 20 个脉冲；enable 自动归零，overruns=0 |
| 采集中切换 ODR | IIO buffer 启用时写入 400Hz 被拒绝 |

最终 1000 帧：

- VI 图像频率 **40.78402491Hz**；IMU **407.84061814Hz**。
- 11012 条 IMU，时间戳、序号均递增，无重复样本，无 >3.75ms 样本间隔；最大间隔 2.50376ms。
- 输出跳帧 0，左右配对丢弃 0。JPEG 含元数据大小 178790–215486 字节，平均 181309 字节。
- 左右 VI 到达时间差 2.306–2.541ms；这不是曝光时刻之差的测量。
- 开流前曾等待主机连接，用户态环形缓冲累计覆盖数为 902；**整个 1000 帧收流期间该计数恒定，新增覆盖 0**。
  首帧携带 1022 条已有 IMU，随后每帧通常 10 条；未固定填充样本数。

该轮收流及后续开关流观测中，驱动 SPI errors / not_ready / late_reads / buffer_drops /
gaps 均为 0。IRQ 线程调度最大延迟 430.72µs，中断到读取完成最大 1.52052ms。
这些是本轮负载下的观测值，不是任意负载的实时上界。

400/40 为标称值，实际频率跟随 IMU 时基；保留真实时间戳，不将频率或每帧样本数强行改为整数。
主机未开流时持续采集，图像只留最新一对，IMU 最多保留 2048 条。
因此长时间关流后的历史覆盖、输出序号跳跃是预期行为，可由 flags / counters 检测。

材料在 `install/tidesense-uvc-validation/`：`release-1000.mjpg`、
`release-1000-summary.json`、`release-1000-decoded/`、`host-release-1000-ssh.txt`、
`reopen-100-summary.json`、`regression-200.bin`、`regression-200.txt` 及本地测试日志。

## 联调修复及边界

修复了 SDK V4L2 依赖、Windows UVC 1.0 描述符兼容、正数 ioctl 成功返回、
空事件 ENOENT、关流与 QBUF 竞态、RNDIS 驱动标识切换和视频进程影响整个 gadget 生命周期等问题。
DWC2 全局 NAK 中断原先会使空闲 RNDIS 端点产生大量中断；现在只在等时 IN 队列等待同步令牌时启用。
图像使用缓存映射读取和硬件 MJPEG；帧间保持 USB 队列调度后，1000 帧完整性检查通过。
早期存在损坏帧的收流文件仅作为调试材料，不能替代最终 `release-1000` 结果。

本轮并行负载是持续 SSH 状态读取，没有证明任意大量 SCP/CPU 负载均无丢帧。
USB 主机若丢掉一帧，也会失去该帧携带的 IMU；第一版不重传。
当前暗场图像噪声明显，本轮验证覆盖数据通路，不验证成像质量或 VIO 精度。

设备原始启动文件和模块备份：`/mnt/data/backup-before-uvc-20261020/`。
原启动 FIT SHA256：`28e39aa1e6a69047cdbe8fd9edad4c5b88a885d98ee216bf18d53c27a3298544`。
开机默认仍为纯 RNDIS，未设置 UVC 自启动；使用包内 run.sh 手动切换。

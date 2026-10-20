# TideSense USB 双目 + IMU 第一版

独立可选模式：SC7U22 400 Hz，硬中断每 10 次输出一次公共 FSIN；
两路 OV9281 各 640×400 / 标称 40 Hz，左右横拼为 1280×400，硬件 MJPEG。
同一 USB 配置保留 RNDIS，设备网络地址仍为 192.168.123.99。
频率跟随 IMU 时基，400/40 是标称值。默认开机仍是 RNDIS，不自动启动 UVC。

## 构建和部署

```sh
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
# 首次从旧内核构建目录升级时，先 clean_kernel，重新载入已更新的板级配置。
build_kernel
clean_osdrv
build_osdrv
scripts/uvc/build.sh
```

需要含 SC7U22 200/400 Hz 切换及本版 UVC 补丁的内核。
板级 defconfig 已开启 MEDIA_SUPPORT、VIDEO_DEV、VIDEO_V4L2 和 USB_CONFIGFS_F_UVC；
检查最终 `.config`，不能仅凭原始 defconfig 中的 UVC 声明判断功能已编入。
部署前备份设备 boot.sd 和匹配模块，
安装 `install/soc_sg2002_tidesense_arm64_sd/rawimages/boot.sd` 并重启；
配套部署本次内核模块（尤其 `dwc2.ko`）及 osdrv 模块，不能只替换 boot.sd。
修改 `/etc/init.d/S77rndis`，以 `insmod /mnt/system/ko/dwc2.ko gadget_desc_dma=0`
加载 USB 模块；板级 rootfs 构建已自动设置该选项。不能用带 CIMG 头的 boot.sd 替代 rawimages 文件。
应用包为 `install/soc_sg2002_tidesense_arm64_sd/tidesense-uvc-a53.tar`，
在设备 `/mnt/data` 解包。

先停止其他相机、VIO、IMU 采集程序；本程序使用相同的相机和 IIO 所有权锁。
运行 `/etc/init.d/S21imu_priority start` 设置 IRQ/SPI 实时优先级。

```sh
cd /mnt/data/tidesense-uvc
./run.sh start
# USB 会重新枚举，SSH 短暂断开；重新连接后：
./run.sh status
tail /mnt/data/tidesense-uvc/uvc.log

# 恢复纯 RNDIS，USB 再次重新枚举
./run.sh stop
```

start/stop 在后台完成，避免 SSH 断开中止切换。日志为包目录中的 `uvc.log`，重启后仍保留。
先在 RNDIS 连接期间准备和验证 UVC 属性，再切换组合描述符。
启动失败会尝试移除 UVC 并恢复原 RNDIS
描述符和连接。要求原始 SDK `rndis.usb0` 为唯一现有 function，其他复合配置明确拒绝。
RNDIS 保留原 function/MAC 和 SDK VID/PID 0x3346/0x1009；两种模式统一使用
Microsoft OS 子兼容标识 `5162001`，匹配 Windows 自带 RNDIS6 驱动。
首次升级可能重新建立网卡配置，需将该 USB 网卡设为 192.168.123.x/24。
板级 rootfs 构建会同步更新 `/etc/run_usb.sh`；只升级旧设备内核时，也需在该脚本
写入 RNDIS compatible_id 后增加同路径的 `echo 5162001 >.../sub_compatible_id`。
切回纯 RNDIS 会保留此兼容标识。量产需使用自己的 VID/PID。

UVC 只公布 USB High Speed 下的 1280×400@40 MJPEG；等时端点每 250µs 最多 3072 字节。
沿用 UVC 1.0 描述符及 26 字节 Probe/Commit 控制块。
不公布无法满足的 Full Speed 视频模式。程序使用 RNDIS 接口 0/1、UVC 接口 2/3 的固定组合顺序。
本版内核支持 configfs `keep_connected=1`：采集程序未打开或退出时保持整个复合设备连接，
避免视频进程的生命周期断开 RNDIS。默认值 0 保持其他 UVC gadget 的原行为。
运行期间持续采集和编码；主机未开流时图像只保留最新一对，IMU 历史最多 2048 条。
SIGINT/SIGTERM 正常退出停止 FSIN/IIO，并恢复启动前的 IMU 频率。
SIGKILL 无法清理；确认无消费者后手动关闭 IIO buffer 和 fsin_enable，再恢复 200 Hz。

## 单独检查采集/编码

保持纯 RNDIS，不切换 USB，采集有限帧到 tmpfs：

```sh
./run.sh capture /tmp/tidesense-test.mjpg 400
```

复制到电脑后检查；也可对主机通过 UVC 接收到的原始 MJPEG 使用同一解析器：

```sh
python3 scripts/uvc/decode.py tidesense-test.mjpg --output /tmp/tidesense-decoded --extract
```

Linux 主机可通过 v4l2-ctl 保存原始流，例如：

```sh
v4l2-ctl -d /dev/videoN --set-fmt-video=width=1280,height=400,pixelformat=MJPG \
  --set-parm=40 --stream-mmap=4 --stream-count=400 --stream-to=host.mjpg
python3 scripts/uvc/decode.py host.mjpg --output host-decoded
```

视频设备编号应从 v4l2-ctl --list-devices 查找。普通预览软件显示左右拼接图；
要获取元数据必须在解码 JPEG 前读取压缩帧，不能从 OpenCV 解码后的像素恢复 APP15。
程序可用 `--quality 1..99` 调整 JPEG 质量，默认 60；启动脚本采用默认值。
拼接前使用缓存映射并失效 VI 缓存，避免 A53 从非缓存内存逐行读取。
内核限制 DWC2 NAK 中断仅在等时 IN 队列等待首个主机令牌时启用，
防止空闲 RNDIS 端点产生中断洪泛。此 SDK DWC2 等时路径不支持多段 SG，
不要开启 `CONFIG_USB_UVCG_SG_TRANSFER`。
TideSense 使用 buffer DMA（`gadget_desc_dma=0`），UVC 预排队 32 个请求，
JPEG 间隙持续发送零长度 USB 事务，避免每帧重新同步时丢失首包。
空事务不会增加 JPEG 内容或生成重复图像；主机关闭开流时停止这些事务。
大文件 SCP 与视频共享 USB 带宽和 CPU，建议限速。

## CPU 占用实测

2026-10-20 在板端实测：双目横拼 1280×400，硬件 MJPEG 质量 60，
图像实际约 40.78fps，IMU 标称 400Hz，RNDIS 保持连接。
主机通过 UVC 接收原始 MJPEG；百分比按板端 Linux 可见的单颗 A53 核心计算。

| 状态 | 全机 CPU 占用 | tidesense_uvc 进程占用 |
| --- | --- | --- |
| UVC 正在传图 | 约 43%（两次分别为 43.36%、42.90%） | 约 26%（25.87%、26.18%） |
| 主机未开流，板端仍持续采集和编码 | 29.47% | 18.60% |

使用 `/proc/stat` 和 `/proc/<pid>/stat` 的计数差计算区间平均值：
传图状态连续采样两次，每次 10 秒；未开流状态采样 8 秒。
全机占用包含 USB 内核工作线程、IMU 中断和 VI/ISP 等开销，不能只用应用进程占用代表总负载。
上述结果对应当前画面和质量设置，未叠加大流量 SCP 或 VIO 任务。

USB 已使用 buffer DMA，运行参数为 `g_dma=1`、`g_dma_desc=0`，descriptor/SG DMA 关闭。
MJPEG 由硬件编码；CPU 仍负责双目横拼、APP15 元数据打包和部分内存拷贝。

## 数据协议 v1

元数据放在每帧 JPEG 的 APP15（FF EF）段、SOI 后面，原图仍是合法 JPEG。
**第一版不修改 UVC payload header，不通过 XU 控制查询运送 IMU。**
JPEG 段长度遵循 JPEG 大端规则；下表中载荷字段均为小端。
所有 ns 时间都是设备 CLOCK_MONOTONIC，不是主机时间或 UTC。

| 字节偏移 | 类型 | 内容 |
| --- | --- | --- |
| 0 | 8 bytes | `TIDESNS\0` |
| 8 / 10 / 12 | uint16 ×3 | 版本 1 / 头长 96 / 样本长 32 |
| 14 | uint16 | flags |
| 16 | uint64 | 图像对采集序号，单进程递增 |
| 24 / 32 | uint64 ×2 | 左/右原始 VI PTS ×1000，单位 ns |
| 40 / 44 | uint32 ×2 | 左/右 VI TimeRef |
| 48 / 52 / 56 | uint32 ×3 | 图像标称 Hz=40 / IMU 标称 Hz=400 / 样本数 |
| 60 | uint64 | 用户态 IMU 环形缓冲累计覆盖数 |
| 68 | uint64 | 输出端累计跳过的采集图像对数，不含首帧之前的图像 |
| 76 / 84 | IEEE754 float64 ×2 | 加速度 m/s²/count、角速度 rad/s/count |
| 92 | uint32 | 保留，0 |
| 96 起 | 样本数组 | 每条 32 字节，见下 |

样本：uint64 交付序号、uint64 硬中断时间 ns、6×int16 原始计数（accel XYZ / gyro XYZ）、
uint32 flags。样本 flags：bit0 表示距前一有效样本 >3.75ms；bit1 表示某轴饱和。
样本序号是软件交付序号，不是芯片硬件序号，不能用于推断所有硬件丢样数。

头 flags：bit0=VI/IMU 单调时钟；bit1=本批样本有 gap 或饱和；bit2=历史发生用户态 IMU 覆盖；
bit3=历史发生左右配对丢弃；bit4=首次输出或 USB 开流恢复。
每批最多 1024 条，通常约 10 条。选择时间 <= max(左右 VI PTS) 的已收到 IMU，
尚未消费的样本保留到后续帧；一个时间更早的 IMU 可能在下一帧送达。
没有固定十条填充或合成时间戳。只有成功写入文件或交给 UVC QBUF 后才消费该批 IMU。
USB 主机丢帧仍可能丢失其所携带的 IMU，主机应检查帧/样本序号；本版不做重传。

图像时间只保存 **VI 到达时间**。不使用 FSIN 时刻替代、不估计曝光中心、不自动应用时间偏移。
曝光和 IMU 内部滤波延迟均由用户自行标定。双目按 VI PTS 差不超过 10ms 配对；
这是软件配对检查，不是曝光同步的独立测量。

## 验证

构建、协议测试和板端结果见同目录 VALIDATION.md。400 Hz 不启用传感器 FIFO；
若 IRQ/读取超过 2.5ms，驱动丢弃并计入 late_reads；不要把未过载的短测视为最坏实时保证。
诊断位于 IIO diagnostics / fsin_diagnostics，包含驱动累计错误和触发计数。

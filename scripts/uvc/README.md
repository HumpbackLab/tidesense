# TideSense UVC

将两路 OV9281 图像横向拼接为一路 MJPEG，通过 USB UVC 输出。每帧 JPEG 同时携带标称 400 Hz 的 SC7U22 IMU 样本。USB 复合设备保留 RNDIS 网络接口，设备地址为 `192.168.123.99`。

连续同步档通过 `tide_cam` 采集双目灰度图。UVC 程序负责横向拼接、硬件 MJPEG 编码和 USB 传输，
与 VIO、录制器及深度程序互斥。

## 设备要求

- SG2002 内核启用 UVC configfs gadget，以及 SC7U22 的 200/400 Hz 切换和 FSIN 支持。
- `dwc2.ko` 以 `gadget_desc_dma=0` 加载，`CONFIG_USB_UVCG_SG_TRANSFER` 关闭。
- 启动前 USB gadget 只有 SDK 的 `rndis.usb0` function；其他相机和 IMU 采集程序已停止。

每次启动向 USB High Speed 主机公布所选的一档 MJPEG。启动和停止会使 USB 重新枚举，SSH 连接可能短暂中断。

| 档位 | 单目采集 | 主机拼接尺寸 | TideSense 板端状态 |
| --- | --- | --- | --- |
| `400p40`（默认） | 640×400 @40，RAW10 | 1280×400 | 已验证 |
| `800p60` | 1280×800 @60，RAW10 | 2560×800 | 已验证，需带 FSIN 输入事件的内核 |
| `480p180` | 640×480 @180，RAW8 | 1280×480 | 需 FSIN 输入事件和 VI 前端模块 |
| `400p210` | 640×400 @210，RAW8 | 1280×400 | 需 FSIN 输入事件和 VI 前端模块 |

连续同步档位默认由右目输出 FSIN，左目跟随。直接调用 `bin/tidesense_uvc` 时可用
`--master left` 选择左目。采集库启用 SC7U22 IIO 的
`events/in_count0_change_rising_en`，将共用的 SG2002 引脚切到输入，并记录同步边沿。
连续档启动时须收到连续配对的双目帧，失败时 `run.sh` 回滚 USB 配置。
两个 RAW8 档需要 SDK 的 `osdrv` 编译出的 VI 模块，加载时传入
`tidesense_fe_only=1`，USB 端点使用 1024 字节。普通档使用模块的默认 ISP 后端模式。

## 构建与部署

在 SDK 根目录运行：

```sh
scripts/uvc/build.sh
```

生成 `install/soc_sg2002_tidesense_arm64_sd/tidesense-uvc-a53.tar`。将包复制到设备并解压到 `/mnt/data`。内核缺少上述功能时，还需部署对应的内核镜像、`dwc2.ko` 和 osdrv 模块；应用包不能代替内核更新。

## 使用

在设备上运行：

```sh
/etc/init.d/S21imu_priority start
cd /mnt/data/tidesense-uvc
./run.sh start 800p60
./run.sh status
./run.sh stop
```

`start` 和 `stop` 在后台完成 USB 模式切换；运行日志位于包目录的 `uvc.log`。停止后恢复纯 RNDIS 模式和启动前的 IMU 频率。

不切换 USB 时，可以将有限帧写入文件：

```sh
./run.sh capture /tmp/tidesense.mjpg 400 800p60
```

复制文件到电脑后，可用 `decode.py` 提取图像与元数据：

```sh
python3 scripts/uvc/decode.py tidesense.mjpg --output /tmp/tidesense-decoded --extract
```

Linux 主机可通过 `v4l2-ctl --list-devices` 查找 UVC 视频节点。普通摄像头软件可显示拼接画面；读取 IMU 元数据需要保留原始 MJPEG 数据。

## 数据格式

每帧是合法 JPEG，SOI 后的 APP15 段以 `TIDESNS\0` 开头。`400p40` 沿用 96 字节头的版本 1。连续同步档使用版本 2，头长 112 字节。两版每条 IMU 样本均为 32 字节，单帧最多 1024 条。多字节整数字段为小端，JPEG 段长度为大端。

固定头包含图像对序号、左右 VI 时间戳和 TimeRef、图像与 IMU 标称频率、样本数、丢帧计数及 IMU 量程系数。样本包含序号、时间戳、加速度和角速度六轴原始计数及状态标志。VI 到达、FSIN 输入中断和 IMU 数据就绪中断都使用设备的 `CLOCK_MONOTONIC`。版本 2 的图像对序号对应 FSIN 边沿，曝光时长由传感器行数换算。估算曝光中心仍需要标定 FSIN 边沿到曝光的偏移和 IMU 的有效延迟。

| 头内偏移 | 字段 |
| --- | --- |
| 0–13 | 8 字节标识、16 位版本、头长、样本长 |
| 14 | 16 位状态标志 |
| 16–47 | 图像对序号、左右 VI 时间戳（ns）及 TimeRef |
| 48–59 | 图像频率、IMU 频率、样本数 |
| 60–75 | IMU 环形缓冲覆盖数、输出端跳过的图像对数 |
| 76–91 | 加速度和角速度换算系数，两个 IEEE 754 binary64 |
| 92–95 | 保留 |
| 96–103（v2） | FSIN 上升沿时间戳（ns） |
| 104–107（v2） | 曝光时长（ns） |
| 108–111（v2） | 保留 |
| 头之后 | IMU 样本，含序号、时间戳、六轴 int16 原始值和 32 位标志 |

头标志分别表示单调时钟、IMU 间隔或饱和、IMU 缓冲覆盖、双目配对丢弃及首次输出或重新开流。样本标志表示采样间隔过长或某轴饱和。若 USB 主机丢帧，该帧携带的 IMU 样本也可能丢失；接收端应检查图像与样本序号。

字段定义和解析实现见 [protocol.c](protocol.c)、[decode.py](decode.py)。板端验证记录见 [VALIDATION.md](VALIDATION.md)。

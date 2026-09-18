# TideSense OV9281 模式与上板验证

默认是 **640×400 RAW10，标称 60fps，全视场子采样**。保留两个可选模式：

| `sensor_cfg.ini` 中的 `name` | 输出 | 方式 |
| --- | --- | --- |
| `OV_OV9281_MIPI_400P_60FPS_10BIT` | 640×400 | 全阵列窗口子采样，默认 |
| `OV_OV9281_MIPI_480P_60FPS_10BIT` | 640×480 | 居中裁切，原画面坐标 (320,160) 起始 |
| `OV_OV9281_MIPI_800P_60FPS_10BIT` | 1280×800 | 全分辨率 |

左右两路均在 `/mnt/data/sensor_cfg.ini` 配置。左侧地址 0x10、2 lane；右侧地址 0x60、1 lane。
三个模式均使用 HTS=1456、VTS=910，ISP/RX 尺寸与传感器输出保持一致。修正 sensor 聚合共享库 `Makefile_full` 对目标文件的依赖，确保增量构建同步更新 `libsns_full.so`。AE 支持降到 30fps（VTS=1820），低帧率下最大曝光随 VTS 调整。

640×400 使用 [ArduCAM 参考驱动](https://github.com/ArduCAM/ov9281_driver/blob/main/ov9281.c) 的全阵列子采样寄存器组，调整 HTS/VTS 以保持当前板级时序。它采用参考驱动的水平 binning、垂直子采样设置，不宣称已验证为双方向平均的纯 2×2 binning。裁切寄存器参考 SDK `docs/OV9281_DataSheet.md` 第 4.2、7.8 节。

## 构建与运行

在 SDK 根目录执行：

```sh
scripts/ov9281/build.sh
```

输出 `install/ov9281-test/capture` 和 `mode_check`。脚本编译当前板的 sensor component、sample common，以及测试程序；后者静态链接 SDK 库、动态链接系统 C 运行库，无需在设备额外安装整套 MPI 动态库。

设备上已部署：

- `/mnt/data/sensor_cfg.ini`：默认 640×400，保留原来的 `hw_sync=1`。
- `/mnt/data/ov9281-update/capture`：实际双路 VI 取帧工具。
- `/mnt/data/ov9281-update/mode_check`：真实驱动回调测试，无传感器 I/O。
- `/mnt/data/ov9281-update/libsns_ov9281.so`、`libsns_full.so`：新动态库。
- `/mnt/data/ov9281-update/backup-before-modes/`：原来的 INI 和 OV9281 动态库。

运行（确认没有其他采集程序占用相机）：

```sh
mkdir -p /tmp/ov9281-test
cd /tmp/ov9281-test
/mnt/data/ov9281-update/mode_check
/mnt/data/ov9281-update/capture
```

`capture` 默认读取 `/mnt/data/sensor_cfg.ini`；也可显式指定配置文件。读取每路 600 帧，检查尺寸与时间戳递增，回读输出尺寸/时序/lane 寄存器，打印 VI/MIPI 状态，并在当前目录保存两张灰度 PGM。SDK CV181x 取帧使用 `CVI_VI_GetChnFrame(0, channel)`，其中 channel=0/1。

切换测试模式无需修改默认文件：

```sh
sed s/400P/480P/g /mnt/data/sensor_cfg.ini > /tmp/sensor-480.ini
/mnt/data/ov9281-update/capture /tmp/sensor-480.ini
sed s/400P/800P/g /mnt/data/sensor_cfg.ini > /tmp/sensor-800.ini
/mnt/data/ov9281-update/capture /tmp/sensor-800.ini
```

增加 `--free-run` 可仅对本次测试关闭 `hw_sync`，不改写 INI。程序退出后停止 VI/ISP；没有新增开机常驻采集服务。

## 2026-09-18 上板结果

设备：192.168.123.99，SG2002 A53，Linux 5.10.4。设备时钟没有校时，日志中的 1970 时间不可作为测试日期。

| 模式 | 每路帧数 | 左/右应用取帧率（VI PTS） | 输出尺寸 |
| --- | --- | --- | --- |
| 640×400 默认 | 600 | 约 60.38 / 60.38fps | 两路正确 |
| 640×480 | 600 | 约 56.19 / 56.19fps | 两路正确 |
| 1280×800 | 600 | 约 60.38 / 60.38fps | 两路正确 |

详情见 `board-test.log`。尺寸回读匹配，数据不是常量缓冲区。模式切换测试中曾观测到单次 WordCount/ECC 错误，检查发现原驱动退出时只关闭 I²C，没有停止传感器输出；现已补上退出停流，避免 RX 重配时传感器继续发送旧模式的数据。最终三个模式依次切换复测时，MIPI ECC/CRC/Header/WordCount/FIFO 错误均为 0，VI LostFrame/VbFail 均为 0；各路连续取帧 600 帧。

640×480 的 60fps 是配置目标，当前双路采集实测未达到。该模式左 VI 计数约 56fps、右 VI 约 61fps；测试程序顺序取左右帧，因此右应用取帧率受左侧限制。自由运行测试也出现同样现象，原因尚未确定，不视为两颗传感器都只能输出 56fps。

驱动回调测试覆盖：默认模式、三种尺寸选择、RX/ISP 尺寸一致、设置 WDR_NONE 不覆盖已选模式、60/30fps 曝光上限，以及非法尺寸/帧率（包括 NaN）拒绝。

限制：

- 用户确认未装镜头。本次只验数据链路，未验证视场、清晰度、双目标定或 VIO 轨迹效果。
- `hw_sync=1` 下可以取帧，但两路 VI 到达时间有毫秒级相位差，尚未验证 FSIN 波形和曝光同步；VI PTS 不是曝光同步测量。不可将本次结果视为双目同步通过。
- 更换模式需要匹配对应的相机内参/矫正映射；640×480 是裁切，不能当作 1280×800 等比缩小。装镜头后需完成双目和 IMU-相机标定。
- SDK 日志有既有的 `devmem: not found` 和退出时 AF 未初始化消息；本次尺寸和取帧验证通过，并未把这些日志认定为已修复。

本次无需更换内核，也未重制整卡镜像。板级镜像脚本现会把当前板级 INI 放入 DATA 分区；已有静态链接相机应用需要重新编译链接，单独替换 `.so` 不会更新其内嵌驱动。

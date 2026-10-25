# tide_cam 验证记录

日期：2026-10-25。设备：TideSense SG2002 A53，双 OV9281，SC7U22 200 Hz / INT1 FSIN。
测试程序均上传到设备 `/tmp`，未替换已部署的 VIO、depth 或录制程序。

## 构建与主机测试

- SDK GCC 6 动态 libc 示例构建通过。
- GCC 11 全静态示例及板端生命周期测试构建通过。
- GCC 11 C++ 外部工程通过安装后的 `find_package(tide_cam CONFIG REQUIRED)`，
  链接 GCC 6 构建的 `libtide_cam.a` 和 SDK archives，生成全静态程序；
  板端运行输出 `cameras=2 mode=640x400`。这验证公共头文件的 C/C++ 链接以及导出依赖。
- 主机 ASan + UBSan 测试通过：参数验证、左右独立时间戳、逐像素帧引用有效性、
  最新帧替换、FIFO 顺序/溢出、重复 release、close BUSY、stop 唤醒和后台故障清理。

## 真实硬件

| 测试 | 图像对 | 实际消费频率 | 结果 |
| --- | ---: | ---: | --- |
| GCC 6，硬件同步，LATEST | 60 | 19.957 Hz | acquired/captured 均为 60，replaced=0，退出码 0 |
| GCC 11 全静态，同进程第 1 次打开，LATEST | 21 | 19.957 Hz | 生命周期测试通过 |
| GCC 11 全静态，同进程第 2 次打开，FIFO | 21 | 19.957 Hz | 帧序连续，生命周期测试通过 |
| GCC 11 全静态，自由运行 | 30 | 20.125 Hz | replaced=0，退出码 0；此模式不保证曝光同步 |
| GCC 11 全静态，持续采集后发送 SIGINT | 14 | 19.957 Hz | 正常停止，退出码 0 |

生命周期测试在后续采集 20 对图像期间一直持有第一对，并对两路原始图像逐字节比对，
确认未被缓冲复用覆盖；停止硬件后再次核对像素仍有效。
同时检查：第二个 open 返回 BUSY 且不影响第一个句柄；持有帧时 close 返回 BUSY；
release 后可正常 close；同进程可重新初始化 SDK；SIGINT/SIGTERM handler 未被库替换。

每轮测试结束后均读取内核 `fsin_enable=0`，未启用 IMU IIO buffer。
首组硬件同步图像的 VI 时间戳为左 `1232291372000 ns`、右 `1232293529000 ns`，
组时间等于左 VI 时间；这是 VI 到达时间差，不是曝光同步误差。

## SDK 行为与范围

首次测试发现 SDK 将 get-frame ioctl 的等待超时统一返回为 `CVI_ERR_VI_FAILED_NOT_ENABLED`。
后端对该返回值和 BUF_EMPTY 做有界重试；超过 3 秒没有有效图像对才报告采集故障，
避免把启动阶段首个 100 ms 等待超时当成通道配置失败。

测试仍可见 SDK 既有 `devmem: not found`、netlink family 及退出 AF 未初始化日志；
本次没有修改厂商 SDK 实现。上述采集和清理测试均通过。

本轮验证采集、时间戳传递、像素生命周期和直接链接，不代表标定/VIO 精度，
也没有测量相较 socket 方案的 CPU 或延迟收益。现有应用尚未迁移到此库。

原始日志：`install/tide_cam-validation/`；构建产物：`install/tide_cam-build/`、
`install/tide_cam-gcc11-static/`、`install/tide_cam-consumer/`。

验证二进制 SHA-256：

```text
GCC 6 tide_cam_capture:
3e4adeba1b0b62e02dff5e6c3c2451b8ca44f8df3cf6f72c17acfe516a1fce86
GCC 11 static tide_cam_board_test:
23577a29741037db3defd775ec00d11d09cde4f37cbcc8755e6c9b570d4a07c2
```

## 多分辨率接入验证（2026-10-25）

检查现有驱动确认三个传感器输出尺寸：640×400 全视场子采样、640×480 中央裁剪、
1280×800 全分辨率。SDK 的 480P/800P 模式名为 60FPS；库使用这些模式选择正确的尺寸，
在启动交付/FSIN 之前将 ISP/传感器采集频率设置为 20 Hz。未修改传感器寄存器表或厂商 SDK。

本轮 GCC 6 和 GCC 11 全静态构建均通过。ASan/UBSan 测试新增三种尺寸的逐像素缓冲边界、
两路图像地址偏移、width/height/stride/data_size 检查，全部通过。

GCC 11 全静态板端程序在同一进程中依次打开三个模式，每种模式分别测试 LATEST/FIFO。
每轮持有首对图像直到后续 20 对完成，逐字节验证持有图像未改变，停止后继续核对。

| 尺寸 | 硬件同步 LATEST | 硬件同步 FIFO | 自由运行 FIFO |
| --- | --- | --- | --- |
| 640×400 | 21 对，19.957 Hz | 21 对，19.957 Hz | 原始验证见上表 |
| 640×480 | 21 对，19.957 Hz | 21 对，19.957 Hz | 60 对，20.125 Hz |
| 1280×800 | 21 对，19.957 Hz | 21 对，19.957 Hz | 60 对，20.125 Hz |

全部运行退出码为 0，replaced=0。新尺寸实际输出为 640×480 / 307200 字节每路、
1280×800 / 1024000 字节每路，stride 分别为 640 / 1280。FIFO 的 time_ref 连续性检查
通过，未用 VI 限帧或软件抽帧伪装 20 Hz。每轮后读取 `fsin_enable=0`、IMU buffer=0。

新增示例参数 `--width`、`--height`。本轮没有接入 60 Hz 采集；不支持的组合仍明确拒绝。
原始日志位于 `install/tide_cam-validation/resolutions/`。

本轮实际上传的 stripped GCC 11 二进制 SHA-256：

```text
tide_cam_res_capture:
d03826b0c5baa2e3060d8966c8ea02a04cc1e021e7bd97543ae385226fc9b375
tide_cam_res_test:
a9cef42affce583b19958bc559fdca9ac0f5f7016ad7cfe741d6cc8e4d1141bd
```

## 同步与自由运行频率语义（2026-10-25）

`mode` 移除帧率，仅包含尺寸和像素格式。默认同步配置中 `free_run_fps={0,0}`，
任何非零设置都会在硬件初始化之前报错；同步触发的名义频率通过只读能力字段查询。
自由运行使用 `tide_cam_config_init_free_run()` 和 `free_run_fps`，接受驱动支持的
1.67..60 fps 范围，支持分数。同步和自由运行使用同一份公共接口定义。

主机 ASan/UBSan 新增验证：同步模式拒绝频率设置、自由运行默认值、零分子/分母拒绝、
上下界与超范围拒绝、整数/分数频率传递，全部通过。
CLI 实测 `--fps 20` 在未传 `--free-run` 时退出码为 2 并明确提示触发源决定频率；
非法分数与超范围值也被拒绝。GCC 6、GCC 11 全静态及外部 C++ 消费工程构建通过。

同步模式对三个尺寸各运行 LATEST/FIFO，每轮 21 对图像，均为约 19.957 Hz，
所有生命周期和帧引用检查通过。自由运行板端测试如下：

| 尺寸 | 请求 fps | 帧对数 | 实测 Hz | 策略 |
| --- | ---: | ---: | ---: | --- |
| 640×400 | 1.67（167/100） | 6 | 1.680 | FIFO |
| 640×400 | 5 | 12 | 5.031 | FIFO |
| 640×400 | 30 | 40 | 30.188 | FIFO |
| 640×400 | 60 | 60 | 60.376 | FIFO |
| 640×480 | 10 | 20 | 10.063 | FIFO |
| 640×480 | 30 | 40 | 30.188 | FIFO |
| 640×480 | 60 | 40 | 60.373 | LATEST |
| 1280×800 | 10 | 20 | 10.063 | FIFO |
| 1280×800 | 30 | 40 | 30.188 | FIFO |
| 1280×800 | 30000/1001 | 40 | 30.171 | FIFO |
| 1280×800 | 60 | 60 | 60.376 | FIFO |

所有运行退出码为 0，captured 等于消费帧数，replaced=0；FIFO 的逐帧 time_ref 及时间连续性
检查均通过。名义频率与实际频率的小偏差与现有驱动的传感器时钟/行周期行为一致，
不保证不同运行负载下的应用交付吞吐量。结束后 FSIN 和 IMU IIO buffer 均为 0。

低频首测发现 VI/ISP 启动时不交付约 30 帧，直接以 5 fps 启动会触发原 3 秒等待上限。
后端改为低频请求先以 20 fps 完成启动，再设置请求帧率并丢弃过渡帧；不延长无图像故障等待。
取图等待、配对阈值和 FIFO 最大间隔按帧周期计算，避免固定 75 ms 校验误拒绝低频数据。
高于 20 fps 的 640×400 请求会选择驱动中的 60 fps 模式，再设置目标频率。

原始日志：`install/tide_cam-validation/rates/`。
本轮实际上传的 stripped `tide_cam_rate_capture` SHA-256：

```text
ae872205207b10a76698580fb1efb8b300b6dc561a2d2baf9f190d7edb945252
```

## 数据录制与 VIO 接入（2026-10-25）

以下应用测试均使用硬件同步约 20 Hz 和 SC7U22 200 Hz，没有使用自由运行。
图像帧数是每轮处理总量，不是帧率。

- `tidesense_record` 和 SchurVINS 实时入口直接链接 tide_cam，包内不再携带
  `schurvins_camera` 或 `fsin.sh`，命令无需相机 INI。
- 应用适配层在 release 库帧前复制到独立 OpenCV Mat，保证 VIO 关键帧和写盘队列的所有权。
  三种尺寸的适配测试在库缓冲反复复用及关闭后逐像素验证保留的 Mat，故障传播测试通过。
- 正常 VIO 加录制处理 60 帧：`tracking=60 imu_fused=60`，IMU 无丢样，录制 COMPLETE。
  回放同一份数据再次得到 60 帧，输出时间戳完全一致，位姿分量最大差约 `2.78e-10`。
  此为输入/回放一致性验证，不是轨迹精度评估。
- 最终打包程序另验证 30 帧正常 VIO 加录制：`tracking=30 imu_fused=30`，约 20 Hz，
  writer 高水位 1/32，退出码 0。传感器关闭后 FSIN 和 IIO buffer 均为 0。
- 原始录制：640×400 为 61 对 / 630 条 IMU；640×480 为 41 对 / 428 条 IMU；
  1280×800 的 1 秒短段为 21 对 / 228 条 IMU。三份数据导出为 PNG/EuRoC/ROS1 bag 后，
  每张原始图像的 SHA-256、IMU 六轴值和采集时间逐条核对通过。
- 1280×800 连续录制达到 16 对写队列上限，明确报错并保留 INCOMPLETE；导出器拒绝该目录。
  本次该尺寸压缩约 128 ms/对、压缩后约 0.80 MB/对，持续 20 Hz 超过本板处理/SD 能力。
  完成的短段不构成长时间持续录制保证。
- 合成 VIO 自检 6/6 帧通过；VIO 录制单元测试覆盖像素、时间戳、IMU 精度、配置快照和错误标记。
- Python 数据导出 12 项、Kalibr 输入转换 8 项、SchurVINS 配置导出 10 项测试通过。
  ROS1 输入转换逐字节保留 Image/Imu，包括已有 Header.seq，正确使用采集时间重建 bag 索引。
  真实 640×400 录制导出的 ROS1 bag 通过 `kalibr.sh --convert-only`，61 对，无孤立帧。
  本次没有对未摆放标定板的场景运行参数求解。
- 通用录制器和带 micro-ROS 的 SchurVINS 均完成构建、全静态 ELF 检查和打包；
  设备验证只使用 `/tmp/tide-cam-consumers-check/`，没有替换板端原有程序。

记录与像素核对结果：`install/tide-cam-consumers-check/verification.json`；
原始日志和录制/导出产物在同目录。

最终包内二进制 SHA-256：

```text
tidesense_record:
89197fbcd08210bb71f7df16b486074ef4ec9ff86de6007a86b93aee282a42d9
schurvins_stereo:
6f2a284b9867ed7dd656125b2a80ce4a046a14d3fdbb37fe712c08f61b353839
```

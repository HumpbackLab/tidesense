# OV9281 实时双目输入

实时双目＋SC7U22 200Hz 惯性入口另见 [IMU.md](IMU.md)。本文描述纯视觉模式。

本入口将 SG2002 的两路 OV9281 接入 SchurVINS 的双目视觉前端，**不接入 IMU**。
保留原来的离线双目＋IMU CSV 和合成自检。当前支持两路均为 640×400 / 20fps 的 INI。

## 设备运行

```sh
cd /mnt/data/schurvins-gcc11-static
# 真实相机输入冒烟测试，处理 120 对；无需实际标定，不输出位姿文件
./run.sh --camera-smoke /mnt/data/sensor_cfg.ini 120

# 安装镜头并完成标定后，运行纯视觉双目；0 表示持续运行，Ctrl-C 停止
./run.sh --camera actual-calibration.yaml /mnt/data/sensor_cfg.ini /tmp/visual-poses.csv 0
```

`--camera` 必须使用实际的 640×400 双目标定，包含两路 `cameras`、内参/畸变和 `T_B_C`。
此模式不需要 `imu_params` 或 `imu_initialization`，也不会产生 IMU 融合结果。
`--camera-smoke` 使用包内明确标为 `test_only` 的占位几何，只检查真实图像进入视觉前端，
即使日志出现 `tracking` 也不代表定位有效。禁止把该 YAML 用于 `--camera` 输出位姿。
两路顺序为左 0x10 / 2 lane、右 0x60 / 1 lane。

相机模式读取 INI 的两路 `hw_sync`，必须相同：

- `hw_sync=1`：采集进程先准备 PWR_GPIO0/PWM8，再初始化两颗相机，最后启动 20Hz FSIN。
  正常退出、Ctrl-C、客户端断开都会停脉冲并销毁 VI/ISP。
- `hw_sync=0`：自由运行，不操作 PWM。按到达时间近邻配对，不保证曝光同步；正式双目推荐 FSIN。

使用前停止其它相机应用。新采集进程使用 `/run/tidesense-camera.lock`，拒绝多个本程序同时初始化设备。
不要并行运行旧 `capture` 或其它 SDK sample，它们不使用此锁。
没有新增开机服务，也没有修改默认 INI。现有板级 loader 必须已释放显示面板对 PWR_GPIO0 的占用，
参见 `../ov9281/README.md`；当前设备已经完成该配置。

## 输入路径与时间戳

```
OV9281 ×2 → CVITEK VI（每路深度 3）→ schurvins_camera
           → 本地 socketpair → 持续接收线程 → 最新一对（容量 1）
           → SchurVINS addImageBundle → 纯视觉前端
```

- `schurvins_camera` 是 GCC 6 构建的 SDK 采集进程，静态链接 MPI 库、动态链接板上已有的 libc。
  SchurVINS 主程序继续使用 GCC 11 全静态构建，保留现有六项优化开关。
- 将 VI 的 Y 平面按真实 stride 复制为连续的 `CV_8UC1`，复制后立即释放 SDK 帧，
  前端持有独立图像内存。没有图像缩放、磁盘中转或 JPEG/PNG 编解码。
- 带版本头的本地二进制协议传输两路尺寸、帧号、各自 PTS 及两个灰度平面。
  协议仅用于同机小端进程，不是网络协议。接收端检查长度、尺寸、递增序号和时间戳；
  坏头、截断或超时会报错并关闭采集。
- 采集端按 VI 到达 PTS 配对；FSIN 下超过 20ms、自由运行超过 25ms 时，丢弃较早的单帧重新配对。
  不假设“顺序读取的一左一右”天然对应同一次触发。帧号保留为诊断信息。
- 左 VI PTS 从微秒转换为纳秒，作为 bundle 时间；右 PTS 保留在协议头。
  当前内核使用单调时钟，程序也检查其与 `CLOCK_MONOTONIC` 的关系。
  **这是 VI 到达时间，不是 FSIN 边沿或曝光中心时间**；相机—IMU 时间标定尚未做。
- 长闲置后的首帧及 VI/ISP 启动阶段不用于前端；在 VI 开始交付后再丢弃 4 对。
- 接收线程持续读取，前端忙时覆盖尚未处理的旧图像对。统计中的 `replaced_pending` 是主动替换数，
  `max_input_age_ms` 是从左 VI PTS 到前端取出该对的最大时间，不包含本次视觉计算。

上游原实现即使 `use_imu=false` 也会调用惯性后端并断言。补丁
`patches/0006-stereo-visual-only.patch` 让该模式使用已有的 SVO 视觉位姿/结构优化器，
不创建或调用惯性后端；`use_imu=true` 保留原 SchurVINS 路径。

## 构建与验证

原 `build.sh` 现在同时构建/打包 `bin/schurvins_camera`、`bin/fsin.sh` 和 `camera-smoke.yaml`。
单独更新采集进程可执行 `./scripts/schurvins/build-camera.sh`。
`SCHUR_CAMERA_HELPER` 和 `SCHUR_FSIN_TOOL` 可覆盖两个辅助程序路径，默认使用包内同目录程序。

接收队列/协议测试（交叉编译后在设备运行）：

```sh
cmake -S scripts/schurvins -B install/schurvins-gcc11-static/core -DSCHUR_BUILD_CAMERA_TESTS=ON
cmake --build install/schurvins-gcc11-static/core -j4
# 将 schurvins_live_camera_test 和 schurvins_camera_mock 复制到板上
./schurvins_live_camera_test ./schurvins_camera_mock
```

板端过载验证：

```sh
SCHUR_TEST_PROCESS_DELAY_MS=100 ./run.sh --camera-smoke /mnt/data/sensor_cfg.ini 80
```

该变量只用于测试，允许 0–1000ms；正常运行不要设置。有限帧数是“前端处理数”，不是采集数。
日志和位姿建议写 `/tmp`，避免 DATA 分区的同步写入干扰耗时。

2026-09-18 实板验证见 `board-test-camera.log`：

| 测试 | 结果 |
| --- | --- |
| FSIN 实际输入 | 120 对进入前端，20.000Hz；待处理替换 0；最大输入年龄约 12.1ms |
| 额外 100ms 处理延迟 | 输入 179 对，前端处理 80 对，替换 98 对，剩余 1 对；最大输入年龄约 69.7ms |
| 自由运行 | 60 对进入前端，约 20.125Hz |
| 离线双目＋IMU 合成自检 | frames=6、tracking=6、imu_fused=6 |
| 协议/队列测试 | 独立像素所有权、最新帧替换、非法尺寸、截断、时间倒退拒绝通过 |

测试时未装镜头；占位几何、灰度噪声上的 tracking/位姿都不能说明真实 VIO 精度。
本次只验输入和视觉前端可运行；实际场景耗时、真实双目标定、IMU 接入与融合另行验证。

最终完整构建后复测：FSIN 120 对约 20.009Hz（VI 到达 PTS），最大输入年龄约 15.0ms；
额外 100ms 延迟下输入 93 对、处理 40 对、替换 52 对，最大输入年龄约 68.9ms；
自由运行 30 对约 20.125Hz。VI 调度会改变到达 PTS，因此小范围帧率估计偏差不代表 PWM 频率变化。
Ctrl-C 清理及部署后短测也通过。旧设备程序备份在主机
`install/schurvins-gcc11-static/backup-before-live-20260918/board-app.tar`；板上空间有限，备份未放 DATA 分区。

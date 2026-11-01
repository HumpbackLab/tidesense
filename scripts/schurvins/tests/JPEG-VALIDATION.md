# VI 直编码与 DDS 图像发布验证

2026-10-31，在当前 SG2002 测试板和主机完成。默认 JPEG 关闭；测试设置质量 80，
双目 640×400、硬件触发约 20 Hz。测试在独立临时目录运行，未替换设备正式程序或标定。

## 接口及内存检查

```sh
cmake -S tide_cam -B install/tide-jpeg-tests \
  -DTIDE_CAM_BACKEND=TEST -DTIDE_CAM_BUILD_TESTS=ON
cmake --build install/tide-jpeg-tests -j4
ctest --test-dir install/tide-jpeg-tests --output-on-failure
```

三个测试通过：既有采集契约、JPEG 契约、真实 SG2002 JPEG 适配层加 SDK 调用替身。
覆盖在调用线程编码、左右与 mono8 同源、单组借用、慢消费者背压、重复归还、编码错误、
等帧时停止、编码过程中停止，以及 stop 后借用数据仍有效。AddressSanitizer 和 UndefinedBehaviorSanitizer 检查通过。
SDK 替身检查传入的就是原 VI 帧描述符及物理地址，返回的 JPEG 地址直接指向 SDK 码流，
并检查共享 ES 模式拒绝开启、右路失败时左路码流与通道的清理。

原生 DDS 与 micro-ROS 的完整 A53 VIO 构建通过；micro-ROS 保留原功能并拒绝开启 JPEG 发布。
A53 原生 VIO 在 QEMU 完成六帧合成自检；原有 TideCamera 图像生命周期检查通过。

## 实际 DDS 消息

使用 `schurvins_jpeg_publisher_test` 和 `jpeg_publisher_test.py`，运行真实 A53 原生 DDS 发布代码，
相机后端使用主机契约测试的替身。QEMU 发布、主机订阅，左右各三条 CompressedImage 的字节、
frame_id 和时间戳正确，另一个 ROS 上下文同时发布的三条已知 PoseStamped 也收到。
测试使用明确的 Cyclone DDS 单播发现和独立 domain，先完成发现再发送，避免把 best-effort 的发现前丢包当成内容错误。

## 板端硬件及应用测试

| 测试 | 结果 |
| --- | --- |
| `tide_cam_jpeg_board_test`，20 对 | 左右 JPEG 均成功；原始 mono8 与 JPEG 组编号和各路时间戳一致 |
| 纯编码每对平均墙钟时间 | 3.437 ms |
| 编码调用线程每对平均 CPU 时间 | 0.945 ms |
| 编码器结果借用期间 stop | 字节校验保持一致，归还后正常 close |
| 首对 JPEG 解码 | 两路均为 640×400，与同一帧 mono8 的平均绝对像素差约 1.69 / 1.77（有损 JPEG） |
| SchurVINS `--camera-smoke 40` + JPEG | 40 帧输入，左右各收到并解码 40 张 JPEG，mono8 待处理帧替换 0 |
| SchurVINS `--vio-smoke auto 60` + JPEG | 60 帧均融合 IMU；左右各收到并解码 63 张 JPEG（含启动阶段 3 对） |
| VIO smoke IMU | 199.539 Hz，最大间隔 5.048 ms，迟到间隔 0、丢弃 0 |
| VIO smoke 最大输入图像年龄 | 5.409 ms |

纯编码时间从 mono8 已就绪后调用 get_jpeg 开始计量，包含两路编码与取码流，
不包含等待下一帧、DDS 发送、VIO 或保存测试图片；这不是 VIO 的净开销对照实验。
smoke 使用 test-only 几何，验证输入、调度及传输，不验证轨迹精度或长期稳定性。
测试结束后确认 FSIN 关闭、相机锁释放。

原始日志、接收元数据和 JPEG/PGM 样本保存在工程根目录的
`install/tide-jpeg-validation/`，包括 `board-test.log`、`camera-board-schur.log`、
`camera-board-dds-results.json`、`board-vio.log`、`board-dds-results.json`、
`jpeg-decode-validation.json` 和 `sanitizer-tests.log`。

## 对完整 SchurVINS 的开关对照

同日追加四轮 `--vio schurvins.yaml auto ... 300`，使用本板完整单级补偿配置，
顺序为 JPEG 关、开、开、关。每轮 300 帧，去掉前 100 帧预热，统计后 200 帧。
使用相同二进制、标定、640×400/20 Hz 输入、中点积分和像素残差设置；开启质量为 80。
全程订阅原生 DDS 位姿和跟踪状态，开启组额外实际接收并解码左右 JPEG。
A53 保持 1 GHz，温度约 38.5–41.6°C，未出现降频。

| 稳态指标（两轮合并或平均） | JPEG 关闭 | JPEG 开启 |
| --- | --- | --- |
| 整个 VIO 进程 CPU 占单核比例 | 36.38% | 42.93% |
| 整核忙碌比例（包含 IRQ 和其它进程） | 40.98% | 47.76% |
| `addImageBundle` 平均墙钟耗时 | 10.923 ms | 11.113 ms |
| `addImageBundle` P95 | 12.575 ms | 12.499 ms |
| `addImageBundle` P99 | 15.620 ms | 13.660 ms |
| VIO 开始时距原始 VI 时间的年龄 P95 | 14.479 ms | 14.483 ms |
| VIO 结束时距原始 VI 时间的年龄 P95 | 27.018 ms | 26.960 ms |
| IMU 全程最大间隔 | 5.061 ms | 5.060 ms |
| 全程最大取图年龄（含启动） | 6.328 ms | 7.276 ms |

进程 CPU 每轮通过独立轻量采样程序读取 `/proc/PID/stat`，由设备
`sysconf(_SC_CLK_TCK)=100` 换算，并用 profile 中的 monotonic 时间选取相同稳态窗口。
整核比例来自同时采样的 `/proc/stat`。进程 CPU 不包含另行记账的 IRQ；整核指标包含。
`SCHUR_PROFILE_CSV` 的 `cpu_ns` 是窗口内整个进程的 CPU 增量，不能当成独立 VIO 主线程 CPU，
因此这里的算法耗时使用 `SCHUR_TIMING_CSV` 的墙钟测量。
年龄计算从标定对齐时间中还原原始 VI 时间，不把相机–IMU 时间补偿误当成额外延迟。

| 轮次 | 进程 CPU | VIO P95 |
| --- | --- | --- |
| 关 1 | 37.49% | 13.081 ms |
| 开 1 | 43.15% | 12.710 ms |
| 开 2 | 42.70% | 12.053 ms |
| 关 2 | 35.28% | 11.928 ms |

四轮均为 `frames=300 tracking=300 imu_fused=300`；IMU 迟到间隔及丢弃均为 0。
开启后的进程平均 CPU 增加约 **6.55 个百分点**，VIO 平均墙钟耗时增加约 **0.19 ms**。
P95/P99 未见升高，但由于每轮是连续的真实采集、输入不完全相同，不能把较低尾延迟解释成 JPEG 改善了 VIO。
这组数据说明当前场景下主要开销在总 CPU，不能据此保证运动场景、其它分辨率、长期运行或轨迹精度不受影响。

JPEG 两轮合计发布 605 对（1210 张），接收并解码 1209 张；开 1 少收到一张右图。
使用的是 best-effort QoS，未定位该张发生在发现阶段还是传输阶段，不能承诺 JPEG 传输无丢失；
库本身没有 JPEG 主动丢帧队列，VIO 的 1200 帧跟踪及 IMU 融合均完整。

原始数据、采样器、分析脚本及各轮 CSV 位于工程根目录 `install/tide-jpeg-impact/`，
汇总为 `results.json`，订阅统计为 `receiver.json`。测试后移除了独立设备目录，
确认 FSIN 为 0、相机锁释放，未替换正式安装。

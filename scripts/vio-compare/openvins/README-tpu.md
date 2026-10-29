# OpenVINS C906＋TPU LK 与前后端并行

完整像素前端迁移现已另行实现，包括均衡、金字塔、补点和整帧 LK 串接，
参见[完整前端说明](README-frontend.md)。本文下面的数据保留为部分卸载对照。

本版本保持原始 FP64 后端，将 LK 窗口采样、Scharr、残差及归约交给 TPU，
C906 负责迭代和收敛。支持关闭卸载、同步卸载、下一帧预计算三种运行方式。
编译选项 `OPENVINS_C906_TPU_LK` 默认 OFF；带此功能的程序运行时也默认走 CPU，
只有 `OPENVINS_TPU_LK=1` 才访问小核和 TPU。

## 并行边界

后端处理第 N 帧时，独立工作线程使用前端快照预处理第 N+1 帧、构建金字塔，
在第 N 帧图像上补点和做左右匹配，随后计算包括新增点在内的时序 LK。
A53 工作线程提交请求后休眠在邮箱 ioctl 中；C906 负责全部迭代，TPU 执行
窗口运算，此时 A53 主线程执行第 N 帧的 FP64 后端。
主线程启动工作线程后立即执行后端，不再默认等待首批请求发起。
图像准备和补点本身仍消耗 A53 时间；若某帧后端很短，剩余 LK 仍需要等待，
不能保证每帧都完全隐藏光流时间。

工作线程拥有独立点、ID 和检测器，只读快照图像，不访问实时 FeatureDatabase，
也不读取或修改相机标定对象。消费下一帧时，主线程等待结果，合入点和 ID，
按原流程做几何检查、更新特征库，再运行后端。不喂入未来 IMU，相机在线
标定仍由原后端处理。全丢点时保留原流程在新图像上重新检测的行为。

每个请求持有独立共享缓冲区，只有完成后才允许复用。池按实际需要分配，
每个缓冲区 2 MiB。超过 160 点的调用会拆分，帧内不丢点。
单个异步快照最多提前一帧；关闭流水线时恢复同步请求，关闭卸载时使用 CPU。

## 当前调度与实际等待（2026-10-29）

`OPENVINS_TPU_KICKOFF_WAIT` 默认 OFF。只有设置为 `1` 才恢复起跑等待，
用于对照实验。下一帧仍必须在 `worker.get()` 处等到光流结果后才能做几何
检查和更新特征库；该等待不是任意删除就能消除的数据依赖。

完整 UZH-FPV `indoor_forward_6`，1970 帧、1781 帧初始化后，原始 FP64 后端。
两组使用相同程序、C906 计时固件和配置；位姿均与原同步 TPU 版本逐行一致。

| 指标 | 不等待首批（默认） | 等待首批（对照） |
| --- | ---: | ---: |
| 全段平均 camera 调用窗口 | 56.97 ms | 61.36 ms |
| 整段 wall 时间（含 SD 读取） | 243.99 s | 250.89 s |
| 初始化后起跑等待 | 0.008 ms（计时开销） | 17.90 ms |
| 初始化后消费结果等待 | 0.066 ms | 0.051 ms |
| C906 LK 实际执行区间 | 12.39 ms | 12.42 ms |
| C906 端 TPU 提交/等待区间合计 | 8.33 ms | 8.37 ms |
| TPU 调用区间与后端阶段的交集 | 3.77 ms | 6.53 ms |
| 主机请求区间与后端阶段的交集 | 6.08 ms | 15.08 ms |

不等待首批时，初始化后每帧主线程消耗约 41.67 ms CPU，其中后端约 31.14 ms，
其余主要是去畸变、RANSAC、特征库操作。工作线程约 23.74 ms CPU：预处理
约 12.21 ms，检测约 5.87 ms，打包、缓存维护及协议等剩余约 5.66 ms。
两者合计约 65.41 ms CPU/帧，不能把 camera 调用的 60.23 ms（初始化后）
倒数当作可持续帧率，因为工作线程有部分 CPU 工作在调用窗口之外进行。

起跑通知位于 ioctl 前，不能证明 C906 已开始执行。带等待时，主机请求计时
开始到 C906 真正进入命令处理平均约 1.98 ms、P95 约 6.56 ms；不等待时
平均约 0.039 ms。主线程被过早唤醒后会争用唯一的 A53，这解释了为何请求
重叠变大，却没有改善整段吞吐。单独的等待线程 CPU 时间也不代表主线程
阻塞的 wall 时间。

### SD 读取会掩盖小核等待

完整 SD 回放里，不等待首批时 camera 调用窗口内进程 CPU 时间占约 99.1%，
消费等待很短，但不能推论实时输入也如此。把第 1000–1039 帧预先缓存到 RAM，
仍按原始前缀初始化、按同一顺序输入、保留原 IMU，1040 帧位姿也逐行一致。
排除缓存窗口两端后，对相同的第 1002–1037 帧得到：

| 指标 | SD 读取 | RAM 缓存 |
| --- | ---: | ---: |
| camera 调用窗口 | 69.49 ms | 85.05 ms |
| 窗口内 VIO 进程 CPU 时间 | 69.29 ms | 73.79 ms |
| 消费结果等待 | 0.046 ms | 15.64 ms |
| 消费等待与 C906 执行交集 | 0 ms | 10.72 ms |

RAM 回放移除了同步读图提供的间隔，小核尾部工作便暴露在 `worker.get()`。
此窗口的进程 CPU/wall 比约 86.8%，确实仍有等待，当前并行尚不充分。
预加载花费在计时前，所以上表只用于比较同一窗口的流水线停顿，不是整段
SD/内存吞吐对比，也不是实时摄像头的测量。

目前每次 LK 请求内部由 C906/TPU 独立完成，整帧仍可能有补点左右匹配、
左时序跟踪、右时序跟踪三次请求，批间还有 A53 处理。将图像预处理、网格
补点及这些请求的串接迁入小核，可按整段前端 **≤50 ms** 的目标验证，但当前
部分卸载版本未实现完整迁移。A53 还剩约 10.5 ms 几何前端收尾，必须在最终整机
测试中计入，不能仅凭小核 50 ms 就认定稳定 20 Hz。

### 计时机制与复现

`OPENVINS_TPU_TIMING=1` 开启可选 C906 计时，需要新计时固件；默认关闭，
普通 LK v3 固件仍可运行。协议在原控制块末尾追加遥测字段，已有字段偏移及
LK 版本保持不变；额外 TPU 区间数组放在保留区域，有编译期边界检查。
C906 记录 LK 入口/出口及每次 TPU 提交和轮询完成时间。用八次短邮箱采样中
最短 RTT 对齐 25 MHz `rdtime` 和 Linux monotonic，并定期重采样；两轮最大
时钟对齐不确定度小于 0.011 ms。TPU 调用区间包含寄存器准备/提交和轮询，
不是 TIU 的 MAC 利用率计数。

主机请求区间包含调用前排队和返回后的调度延迟，不能当作硬件计算重叠。
CSV 新增 `tpu_kickoff_wait_ms`、`tpu_consume_wait_ms`、后端/工作线程 CPU
时间、C906/TPU 区间及消费等待交集等字段。前端阶段 wall 时间包含被主线程
抢占和等待设备的时间；按 CPU 时间评估 A53 工作量。

板上计时固件及工具放在独立的 `timing-tools`，普通入口继续兼容原固件：

```sh
cd /mnt/data/openvins-c906-tpu
C906_TOOLS_DIR=/mnt/data/openvins-c906-tpu/timing-tools \
OPENVINS_TPU_TIMING=1 OPENVINS_TPU_KICKOFF_WAIT=0 \
OPENVINS_TPU_TRACE=timing-results/gate0-trace.csv \
./live/with-tpu.sh ./run_tpu_record.a53 \
  config-fpv/estimator_config.yaml dataset timing-results/gate0.csv 0 0
```

设 `OPENVINS_TPU_KICKOFF_WAIT=1` 可运行对照。`OPENVINS_PRELOAD_FIRST=1000`
和 `OPENVINS_PRELOAD_COUNT=40` 只用于离线 runner，在计时前缓存指定输入窗口；
最多 100 对，注意内存占用，默认不缓存。原始图像和零掩码都保留，640×480
每对约 1.23 MB。`OPENVINS_TPU_TRACE` 输出主线程阶段、工作线程阶段、请求及
C906 区间；每 20 帧记录一帧详细 TPU 区间，所有帧仍参与汇总。

原始结果、汇总 JSON、时序图位于 `install/openvins-c906-tpu/timing-results/`：

```sh
python3 scripts/vio-compare/openvins/analyse_tpu_timing.py \
  install/openvins-c906-tpu/timing-results \
  --reference install/openvins-c906-tpu/results-a53/sync.csv
PYTHONPATH=/usr/lib/python3/dist-packages /usr/bin/python3 -S \
  scripts/vio-compare/openvins/plot_tpu_timing.py \
  install/openvins-c906-tpu/timing-results
```

绘图需要与 NumPy 兼容的 Matplotlib；本机使用系统 NumPy 1.21/Matplotlib 3.5
环境生成。完整两组及 RAM 窗口的主机模拟接口测试也保持原 CPU 位姿逐行一致。

## 构建

需要已经构建 TideSense SDK、A53 静态 OpenCV/Boost/Ceres，并同步过固定版本
OpenVINS。使用独立源码和构建目录，不覆盖现有 FP64 安装包：

```sh
scripts/c906/build.sh --lk-probe
python3 scripts/vio-compare/openvins/build_tpu.py --arch a53
python3 scripts/vio-compare/openvins/build_live.py \
  --core-build install/openvins-c906-tpu/build-a53 \
  --output install/openvins-c906-tpu/live/bin/openvins_live \
  --micro-ros on
```

固件和适配器均使用默认 16 点批次。不要将其他批次的实验固件与本适配器混用。
`build_tpu.py` 从锁定的上游版本应用维护补丁，检查协方差为 MatrixXd，且编译
参数不含完整/局部 FP32 后端实验。交叉编译产物是静态程序；部署时使用
`openvins_live.stripped`，在设备中命名为 `bin/openvins_live`。

本机可以构建 CPU 模拟接口来检查流水线顺序：

```sh
python3 scripts/vio-compare/openvins/build_tpu.py --arch host
```

主机版本仍执行 OpenCV LK，不模拟 TPU 的 BF16 数值，也不能用于预测 A53 速度。

## 设备实时运行

本次安装目录为 `/mnt/data/openvins-c906-tpu/live`。`config/` 由设备现有的
SchurVINS 标定文件转换，保留 IMU 外部修正、200 Hz 采样、左右旋转约定和
标定时差；`num_pts=150` 是两颗相机合计目标，每颗约 75 点。

```sh
cd /mnt/data/openvins-c906-tpu/live
OPENVINS_MICRO_ROS=0 ./with-tpu.sh ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose.csv 0
```

`with-tpu.sh` 会加载兼容小核固件，暂时卸载 Linux TPU 驱动，加载独占桥接模块，
运行应用；退出或收到 SIGINT/SIGTERM 后恢复普通小核固件及原 Linux TPU 驱动。
若桥接模块已被其他实验加载，脚本拒绝接管。使用前停止其他摄像头或推理任务。
小核工具默认位于 `/mnt/data/c906-reload`，可用 `C906_TOOLS_DIR` 指定位置。

同步卸载对照：

```sh
OPENVINS_TPU_PIPELINE=0 OPENVINS_MICRO_ROS=0 ./with-tpu.sh ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-sync.csv 0
```

CPU 对照无需接管 TPU：

```sh
OPENVINS_TPU_LK=0 OPENVINS_MICRO_ROS=0 ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-cpu.csv 0
```

需要 ROS 位姿时，将 `OPENVINS_MICRO_ROS=1`，Agent 和订阅方式沿用
[实时运行说明](README-live.md)。micro-ROS 与 LK 卸载分别控制。

实时流水线在初始化后保留一个前瞻图像对，通常增加约一个相机周期的延迟，
20 Hz 时约 50 ms；处理跟不上输入时，保留图像的年龄还会增加。初始化前
没有后端可重叠，因此继续消费最新帧，不额外等待前瞻输入。输入仍使用现有 latest-pair 策略，队列不会无限增长；CSV 的 sequence
和 frame_age_ms 可用于检查跳帧与图像年龄。位姿时间戳仍对应当前被更新的
图像，不使用前瞻图像的时间戳。

启动日志会显示 `tpu_lk=1 lookahead_pipeline=1`。CSV 额外记录缓存命中点数、
打包、请求往返、等待线程 CPU 时间和与后端的重叠时间。`tpu_overlap_ms`
是下一帧请求准备调用 ioctl 到返回的时间区间与上一帧后端时间区间的交集，
在结果被消费时记账，包含调用前调度、邮箱和唤醒开销；它不是 TPU 硬件忙碌
时间的测量，不能当作纯 TPU 算子耗时。

## 离线回放

`run_record` 的接口为：

```text
run_record CONFIG RECORD_DIR POSES.csv FRAME_LIMIT [RIGHT_ROTATION]
```

`RECORD_DIR` 使用 `stereo.csv`、`imu.csv` 和相对路径图像，IMU 为已经转换到
合适输入域的 SI 数据。右图默认旋转 180°，用于当前 TideSense 原始图像；
UZH-FPV 使用 `RIGHT_ROTATION=0`。`FRAME_LIMIT=0` 表示整段。

设备已放置完整 UZH-FPV `indoor_forward_6` 输入和测试配置：

```sh
cd /mnt/data/openvins-c906-tpu
./live/with-tpu.sh ./run_tpu_record.a53 \
  config-fpv/estimator_config.yaml dataset replay.csv 0 0
```

此输入的 `num_pts=150`、后端为 FP64，相机标定在线选项沿用此前 UZH 配置。
TPU 仅参与视觉前端。

## 上一轮完整回放结果（带起跑等待）

2026-10-28，SG2002 A53 1 GHz，单线程 OpenCV，同一份 1970 帧输入。
各组全部完成，均有 1781 帧初始化后输出；后端保持 FP64。

| 模式 | 全段平均计算窗口 | 初始化后平均 | P95（初始化后） | ATE RMSE |
| --- | ---: | ---: | ---: | ---: |
| CPU | 68.52 ms | 72.86 ms | 133.42 ms | 0.2951 m |
| TPU 同步 | 70.99 ms | 75.70 ms | 138.38 ms | 0.2972 m |
| TPU 初版：只提前跟踪已有点 | 66.61 ms | 70.85 ms | 129.89 ms | 0.2972 m |
| TPU 完整快照流水线 | 60.19 ms | 63.75 ms | 119.01 ms | 0.2972 m |

完整流水线相对 CPU 平均计算窗口降低约 12.2%，相对同步卸载降低约 15.2%，
相对初版降低约 9.7%。初始化后，时序光流缓存命中平均约 112.2 点/帧，
全部请求平均约 118.6 点/帧（还包含补点时的左右匹配）。平均约 15.2 ms
请求区间与真实后端重叠，初版约 8.9 ms；等待线程平均 CPU 时间约 0.25 ms/帧。
请求往返平均约 18.0 ms，包含后台线程被后端抢占后延迟唤醒的时间，
不能与同步模式约 12.6 ms 的往返差值解释为 TPU 算子变慢。

主线程在首个请求开始发起时进入后端。在单 A53 上，下一帧预处理、补点和
图像打包仍消耗 CPU；较短后端也无法完全覆盖整批 LK。对照实验中，若启动
工作线程后立即跑后端，平均请求重叠只有约 6.6 ms，许多请求落到后端结束后。
该对照的 SD 回放 wall 时间约 244.6 秒，略短于明确对齐提交时序的 248.0 秒，
说明文件读取等待也能掩盖异步工作；重叠数字本身不等同于吞吐收益。
该实验保存在 `pipeline-fullfront-unphased.csv/log`，目前默认使用明确对齐的版本。

TPU 同步与并行的初始化标记、位置及四元数逐行完全一致，说明当前重排没有
改变 TPU 路径的估计结果。CPU 与 TPU 的轨迹有差别，符合前端 BF16 不逐位
等价的预期。ATE 只在共同的 816 个真值覆盖帧上计算，使用原有的
-16.684572 ms 时间对齐及 SE(3) 对齐，不拟合尺度，不代表整段的真值精度。

表中计算窗口为每次 `feed_measurement_camera` 的 wall 时间，流水线中同时
包含上一批结果消费、当前后端及下一批提交，不能当作单帧从曝光到位姿的延迟。
图像文件读取在窗口之外，工作线程仍可继续执行，因此还应看整段 wall 时间：
CPU 约 264.7 秒、同步 TPU 约 268.9 秒、完整流水线约 248.0 秒，完整流水线
相对 CPU 缩短约 6.3%。SD 输入约 1.2 GB，实际回放吞吐约 7.9 帧/秒；
不能把计算窗口对应的约 16.6 帧/秒当作 SD 回放或实时相机的实测帧率。

PC 使用 CPU 模拟接口的三组 1970 帧回放完成；完整快照及提交时序调整后又
分别验证了整段，初始化标记和位姿与原 CPU 结果逐行完全一致。
它验证接口顺序，不是硬件速度或 BF16 精度测试。

初版设备实时入口另做了 50 帧静止场景检查：第 40 帧初始化，消费帧率约
19.95 Hz，无序号跳帧；IMU 约 199.54 Hz，最大间隔约 5.04 ms。实际完成
106 次 TPU 请求，退出后自动恢复普通小核固件及 Linux TPU 驱动。
这次短测试不代表激烈运动场景也能持续 20 Hz。日志位于设备安装目录的
`smoke.log`、`smoke-poses.csv`，本机保留 `install/openvins-c906-tpu/live-smoke-run.log`。

完整快照版本部署后也检查了 50 帧实时入口和资源恢复。当前现场 TPU 版本
未初始化，平均 camera_ms 约 199.8 ms；同现场关闭 TPU 的 CPU 对照也未初始化，
平均约 193.9 ms。摄像头本身仍约 19.95 Hz，IMU 约 199.54 Hz，但应用消费
只有约 5 Hz，发生跳帧；本次没有进入后端并行，不能作为实时流水线速度验证。
日志在设备 `smoke-fullfront*.log`、`smoke-fullfront*-poses.csv`，本机同名前缀
文件也有保存。随后修正了初始化前不必要的前瞻保留行为，并复测 50 帧：平均约 203.2 ms，
仍未初始化，但最大图像年龄从约 375.3 ms 降至 54.5 ms；退出后驱动和固件
正常恢复。此最终复测为 `smoke-fullfront-final.log` 及对应 CSV。

原始 CSV、日志、比较 JSON 位于 `install/openvins-c906-tpu/results-a53/`；
本机顺序验证位于同目录下的 `results-host/`。汇总可复现为：

```sh
python3 scripts/vio-compare/openvins/compare_tpu.py \
  install/openvins-c906-tpu/results-a53 \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv
```

# OpenVINS 完整像素前端卸载

该实验将直方图均衡、五级灰度金字塔、FAST/NMS、网格补点、亚像素角点、
补点左右匹配及左右时序 LK 迁至 C906＋TPU。C906 负责流程、检测和收敛，
TPU 负责整数 LUT、Gaussian 及 LK 窗口计算。A53 保留去畸变、RANSAC、
特征库维护和原始 FP64 后端。后端没有启用完整或局部 FP32。

## 开关与通信

编译宏 `OPENVINS_C906_FRONTEND` 默认 OFF，依赖 `OPENVINS_C906_TPU_LK`。
即使编译支持，普通 `run-live.sh` 仍默认 CPU；运行时同时设置
`OPENVINS_TPU_LK=1 OPENVINS_TPU_FRONTEND=1` 才启用完整卸载。
`OPENVINS_TPU_PIPELINE=0` 可关闭前瞻流水线，保留同步完整卸载。

`OPENVINS_C906_FRONTEND_EARLY` 是独立的提前预处理编译开关，默认 OFF；
`build_tpu.py --frontend --early-pipeline` 启用。编入后，完整卸载运行时默认
提前预处理，设 `OPENVINS_TPU_FRONT_EARLY=0` 可恢复原调度作对照。

提前调度在第 N 帧收到小核结果后，立即上传 N+1 帧并启动均衡/金字塔；
A53 同时对 N 帧做去畸变、RANSAC 和特征库更新，结束后发布筛选好的点和 ID。
小核随后继续补点和 LK，A53 执行 N 帧后端。图像及点快照分开发布到不同
缓存行，但仍只发送一次邮箱请求；前端不会使用未经过几何筛选的点。
小核等点快照有 500 ms 超时，主机等待工作线程时的异常也会传出。
新协议为 v3，必须配对更新固件和应用。

`FRONT_FAST_FILL` 独立控制小核前端的对齐 64 位填充，避免 SDK 逐字节
`memset` 的开销。通过 `C906_FRONT_FASTMEM=ON` 构建启用，不替换全局
内存函数，不改变像素、特征数、角点/光流阈值或后端精度。

每帧上传两张原始灰度图、掩码和点/ID 快照，只发一次邮箱请求。小核缓存
前后两帧的双目金字塔，完成预处理、补点及三组光流后一次返回；中间无需
A53 串接 LK 请求。使用固定 12 MiB ION 缓冲区，一次仅允许一帧在途。
第 N 帧后端与第 N+1 帧像素前端重叠，消费时仍需要等待未完成的前端。
单帧前瞻会增加约一个输入周期的延迟，摄像头适配器仍使用 latest-pair 策略。

当前接口限制：按顺序输入相机 0、1；640×400 或 640×480；灰度 CV_8UC1；
NONE/HISTOGRAM 两种均衡模式，不支持 CLAHE；每相机目标最多 120 点，
网格每边最多 16 格且总格数不超过目标点数，最小间距 4–128 像素。
生产配置使用总计 150 点、16×15 LK 和失败点过滤。

## 构建

```sh
C906_FRONT_FASTMEM=ON C906_LK_DUAL_BATCH=ON scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/openvins/build_tpu.py --frontend --early-pipeline --sparse-nms --lazy-mask --latest-intrinsics --lk-batch 16 --lk-roi-cache --lk-dual --qr7 --arch a53 --jobs 4
python3 scripts/vio-compare/openvins/build_live.py \
  --core-build install/openvins-c906-tpu/build-a53 \
  --output install/openvins-c906-frontend/live/bin/openvins_live --micro-ros on
```

小核程序只在实验固件中启用 C++ 像素内核，并增加命令任务栈；BOOT 分区和
正常开机固件没有改变。SDK 原 `memset()` 缺失返回值，编译器优化后的数组
初始化会使用错误指针，因此修复为返回 `dest`，并在构建中重新安装 common 库。
修复单独保存于 `scripts/c906/freertos-memset.patch`；小核适配钩子保存在
`scripts/c906/freertos-hooks.patch`，已集成的源码不需重复应用。

## 设备使用

独立目录 `/mnt/data/openvins-c906-front`，`live/config` 沿用设备已转换的
TideSense 标定及 IMU 修正。停止其他摄像头/TPU 用户后：

```sh
cd /mnt/data/openvins-c906-front/live
OPENVINS_MICRO_ROS=0 ./run-front.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-front.csv 0
```

需要 ROS 位姿时改为 `OPENVINS_MICRO_ROS=1`，Agent 和订阅方式见
[实时说明](README-live.md)。启动日志显示 `frontend=1`；CSV `front_calls`
表示每帧一条完整请求，`front_*_ms` 区分上传 CPU 与小核各阶段耗时。
旧 `tpu_hits/calls` 属于逐组 LK 路径，完整卸载时这些计数为零是正常的。

`run-front.sh` 临时重载兼容小核固件、接管 TPU，退出后恢复普通固件和原
Linux TPU 驱动。工具位于 `../tools`，也可通过 `C906_TOOLS_DIR` 指定。
直接运行 `run-live.sh` 且不设置 TPU 环境变量即可使用原 CPU 路径。
不要同时运行两份卸载程序。

## 对照与结果

2026-10-29，A53 1 GHz、C906_0 750 MHz、TPU FAB 900/AXI 700 MHz，
UZH-FPV indoor_forward_6，1970 帧，150 点，原始 FP64 后端。

* 主机 CPU 模拟完整卸载的异步、同步回放，均与原 CPU 轨迹逐行完全一致。
* 板上 6 组真实图像的各级金字塔像素、补点坐标及 ID，与 OpenCV/原检测器
  对照一致（640×480、FAST50）。640×400/FAST20 的像素、点数和 ID 也一致；
  一个亚像素输入舍入差异为 0.0000076 px，经过 BF16 LK 后表现为 0.015 px。
  两条路径使用相同坐标重测，LK 输出差异为零；测试不会通过放宽光流阈值掩盖差异。
  硬件 LK 使用已有 BF16 窗口计算，因此不要求与 CPU 轨迹逐位一致。
* 完整板上回放初始化后 1781 帧，SE(3) 对齐、不缩放的 ATE 为 0.287 m，
  原同步 TPU-LK 为 0.297 m，原 CPU 为 0.295 m；共同真值覆盖 816 帧，
  时间平移 -16,684,572 ns。差异不宜解释为精度提升。
* 小核完整前端平均 41.05 ms、P95 65.33 ms、最慢 91.98 ms，约 30.7% 超过
  50 ms。均衡 12.14 ms、金字塔 1.95 ms、补点 14.54 ms、LK 11.96 ms。
  A53 上传约 1.29 ms CPU/帧，原部分卸载工作线程约 23.74 ms CPU/帧。
* 整段含 SD 的回放 210.21 s，原不等待首批的部分卸载为 243.99 s。
  camera 调用窗口不包括全部输入读取/前瞻工作，不能取其倒数宣称实时帧率。

相同 RAM 窗口（第 1002–1037 帧，保留原前缀初始化）对照如下：

| 指标 | 原部分卸载 | 完整像素前端卸载 |
| --- | ---: | ---: |
| 完整帧周期 | 85.98 ms | 75.26 ms |
| 吞吐 | 11.63 Hz | 13.29 Hz |
| camera 调用窗口 | 85.05 ms | 74.91 ms |
| 主线程 CPU | 47.65 ms | 45.93 ms |
| 窗口内进程 CPU | 73.79 ms | 47.51 ms |
| 消费前端结果等待 | 15.64 ms | 27.44 ms |

新版本此窗口小核前端平均 54.68 ms、P95 64.99 ms，后端 CPU 33.61 ms。
RAM 回放与同版完整 SD 回放的前 1040 帧轨迹逐行一致。帧周期下降约 12.5%，
窗口内进程 CPU 下降约 35.6%，但前端自身变慢使等待增加。

最终设备实时冒烟测试 100 帧，61 帧初始化后更新，摄像头/消费均为 19.95 Hz、
IMU 199.54 Hz、无 sequence gap，初始化后 camera 调用平均 31.31 ms。
测试场景较静止且仅约 6.1 s，
不能代表强运动或长期性能；退出后已恢复普通固件/驱动。

2026-10-29，随后在同一程序、同一配置和相同 RAM 窗口上，分别验证两项优化：

| 指标 | 原调度/普通填充 | 提前预处理 | 提前预处理＋快速填充 |
| --- | ---: | ---: | ---: |
| 完整帧周期平均 | 75.49 ms | 67.32 ms | 64.07 ms |
| 帧周期 P95 | 123.93 ms | 117.23 ms | 113.09 ms |
| 吞吐 | 13.25 Hz | 14.85 Hz | 15.61 Hz |
| A53 消费前端结果等待 | 27.75 ms | 19.48 ms | 16.15 ms |
| C906 前端实际计算，扣除等快照 | 54.80 ms | 54.89 ms | 50.67 ms |
| C906 等待点快照 | 0 | 3.08 ms | 3.54 ms |
| 直方图均衡 | 12.19 ms | 12.20 ms | 9.14 ms |
| 补点/亚像素精化 | 22.40 ms | 22.42 ms | 21.05 ms |
| LK 合计 | 17.71 ms | 17.79 ms | 17.95 ms |
| FP64 后端 wall | 35.03 ms | 33.49 ms | 33.55 ms |
| A53 主线程 CPU | 45.94 ms | 46.02 ms | 46.11 ms |

三组前 1040 帧轨迹逐行一致，也与原完整硬件回放的对应前缀一致。新调度的
主机完整 1970 帧回放与原 CPU 参考逐行一致。最终优化版在板上也完成了完整
1970 帧回放，1781 帧初始化后更新，轨迹与优化前硬件参考逐行完全一致，
每帧恰好一条请求。完整回放的前端计算平均 37.31 ms、P95 60.74 ms，
其中均衡 9.13 ms、金字塔 2.05 ms、补点 13.59 ms、LK 12.07 ms。
最终实时冒烟测试 100 帧，61 帧初始化后更新，摄像头及消费均为 19.95 Hz，
IMU 199.54 Hz、无跳帧；静止/ZUPT 场景不能代替强运动吞吐测试。最终帧周期缩短约 15.1%，吞吐
提高约 17.8%，A53 等待减少约 41.8%；后端及几何检查的数值精度没有改变。
发布点快照只消耗约 0.021 ms A53 CPU。`front_total_ms` 从新协议开始包含
`front_snapshot_wait_ms`，比较计算耗时应扣掉该等待；各阶段 P95 不能直接相加。

原始三组文件保存在 `install/openvins-c906-frontend/early-results/board/`，汇总：

```sh
python3 scripts/vio-compare/openvins/compare_front_pipeline.py \
  install/openvins-c906-frontend/early-results/board \
  --reference install/openvins-c906-frontend/front.csv
```

对照组合分别是 `OPENVINS_TPU_FRONT_EARLY=0`＋无快速填充固件、
`OPENVINS_TPU_FRONT_EARLY=1`＋无快速填充固件，以及提前调度＋快速填充固件。
无快速填充固件使用 `C906_FRONT_FASTMEM=OFF scripts/c906/build.sh --frontend-probe`
生成；默认 `--frontend-probe` 已启用快速填充，其余普通固件不受影响。

这是已验证的完整像素前端迁移，尚未保证所有帧 50 ms，也未保证稳定实时 20 Hz。
需要进一步优化 C906 补点、均衡和强运动时的 LK 尾延迟。

离线复现（右图已经符合 FPV 方向，因此最后参数为 0）：

```sh
cd /mnt/data/openvins-c906-front
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 \
  ./live/with-tpu.sh ./run_front.a53 \
  /mnt/data/openvins-c906-tpu/config-fpv/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset results/front.csv 0 0
```

设置 `OPENVINS_PRELOAD_FIRST=1000 OPENVINS_PRELOAD_COUNT=40`，将帧数限制设为
1040，可复现 RAM 窗口，避免 SD 读取隐藏前端等待。详细原始文件位于
`install/openvins-c906-frontend/`。像素内核带 OpenCV/FAST 许可说明，见
`probes/c906-frontend/NOTICE`，没有更改原检测和亚像素算法以换取速度。

结果汇总可重现：

```sh
python3 scripts/vio-compare/openvins/analyse_frontend.py \
  install/openvins-c906-frontend \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --baseline-memory install/openvins-c906-tpu/timing-results/memory.csv
```

## 第二轮：NMS、掩码及 TPU 固定配置

新增三个独立编译开关，均保持点数、阈值、迭代上限和 FP64 后端：

| 内核宏 | 固件构建环境变量 | 行为 |
| --- | --- | --- |
| `FRONT_SPARSE_NMS` | `C906_FRONT_SPARSE_NMS` | 保留光栅顺序及严格八邻域比较，仅遍历 FAST 候选，不再扫描整块分数图 |
| `FRONT_LAZY_MASK` | `C906_FRONT_LAZY_MASK` | 先完成原有点过滤，仅在需要补点时复制/绘制检测掩码 |
| `FRONT_TPU_BASE_CACHE` | `C906_FRONT_TPU_BASE_CACHE` | 每帧首次提交完整基址配置，复位后读回验证；帧内后续提交复用相同配置 |

默认 `--frontend-probe` 启用上述三项；指定对应环境变量为 `OFF` 可分别关闭。
FreeRTOS CMake 中对应 `TIDESENSE_C906_FRONT_*` 选项仍默认 OFF。
TPU 配置复用限定在现有独占模式内，每帧重新验证，失败时保留原全量配置路径；
必要的 TIU/TDMA 复位、启动和完成检查均保留。CPU 模拟内核分别使用
`build_tpu.py --sparse-nms --lazy-mask`；TPU 配置缓存仅作用于小核。

增加 `FRONT_PROFILE`，通过 `C906_FRONT_PROFILE=ON` 编译可选细分计时，默认 OFF。
遥测使用原控制块保留区域，v3 图像/点快照协议及其字段偏移不变。
CSV 追加 `detail_*`：掩码、FAST、NMS、排序、精化、直方图、LUT、边界、
LK 准备/执行/求解和提交次数。`detail_tpu_setup_ms` 为寄存器配置等开销，
`detail_tpu_wait_ms` 为提交后到完成的轮询区间，包含 DMA 和 TIU，不能当作
纯 TIU 算术时间。`detail_flags` 的 bit 0 表示细分计时已开启；未开启时这些
细分时间为零，原有 `front_*` 总计时仍有效。bit 6 表示本帧基址保持性验证通过，
bit 7 表示使用原配置回退路径。

同一 FPV RAM 窗口（1002–1037 帧）独立开关对照，所有前缀轨迹逐行一致：

| 版本 | 前端计算平均 | 帧周期平均 | 吞吐 |
| --- | ---: | ---: | ---: |
| 本轮基线，带细分计时 | 50.95 ms | 64.22 ms | 15.57 Hz |
| 仅候选 NMS | 44.17 ms | 58.92 ms | 16.97 Hz |
| 仅按需掩码 | 50.73 ms | 64.02 ms | 15.62 Hz |
| 仅 TPU 基址复用 | 50.43 ms | 63.90 ms | 15.65 Hz |
| 三项组合，带细分计时 | 43.39 ms | 58.34 ms | 17.14 Hz |
| 最终组合，关闭细分计时 | 43.02 ms | 58.27 ms | 17.16 Hz |

本轮最终等待 15.95 → 10.21 ms，帧周期 P95 113.65 → 106.87 ms，仍非稳定
20 Hz。NMS 从约 6.95 → 0.053 ms；掩码构建在完整初始化后前缀平均减少
约 1.04 ms，在该强运动窗口减少约 0.24 ms；TPU 配置约 2.60 → 2.13 ms。
组合版的分阶段时间以 `opt2/board/kernel-comparison.json` 为准。

尝试过的整套金字塔缓存清理和局部描述符刷新，只各减少约 0.2 ms，整帧未获益，
已撤销。按历史迭代次数分组虽将提交数约 848 → 786，但整帧仅快约 0.5 ms，
也未保留，避免增加历史状态。数据中 `flow`、`patch`、`group` 均为已撤回的实验。

最终关闭细分计时的固件完成了 1970 帧板上回放，轨迹与优化前硬件参考逐行一致。
同一次回放在计时前缓存 1000–1039 帧，兼顾完整轨迹验证及排除读图间隔的速度
测量；其余帧仍从 SD 读图，整段 wall 时间不用于评估计算吞吐。
主机开启 NMS/按需掩码的 1970 帧回放也与原 CPU 参考逐行一致。

原始 CSV/日志/固件和比较结果在 `install/openvins-c906-frontend/opt2/`：

```sh
python3 scripts/vio-compare/openvins/compare_front_kernels.py \
  install/openvins-c906-frontend/opt2/board \
  --reference install/openvins-c906-frontend/early-results/board/full.csv

# 固件全部关闭新增优化的基线，已有 fast-fill 仍启用
C906_FRONT_PROFILE=ON C906_FRONT_SPARSE_NMS=OFF C906_FRONT_LAZY_MASK=OFF \
C906_FRONT_TPU_BASE_CACHE=OFF scripts/c906/build.sh --frontend-probe

# 最终固件：默认三项开启，细分计时关闭
scripts/c906/build.sh --frontend-probe
```

最终设备实时测试 100 帧，完成初始化（6 帧初始化后更新）；摄像头/消费均为
19.95 Hz，IMU 199.54 Hz，无 sequence gap，退出后普通固件和 Linux TPU
驱动已恢复。该短静止测试只验证实时入口，吞吐结论采用上面的 FPV RAM 窗口。


## LK 寻址与最新内参共享

`LK_TPU_FAST_ROI` 优化 C906 上的 LK 窗口寻址，使用有界整数转换与负小数修正
代替重复的 `floorf()` 调用，并内联到初始化及迭代循环。窗口、BF16 权重、
收敛条件、迭代上限及有效点判定保持原规则。FreeRTOS 的
`TIDESENSE_C906_LK_FAST_ROI` 默认 OFF，`build.sh --frontend-probe` 默认 ON；
用 `C906_LK_FAST_ROI=OFF` 构建对照固件。

FPV 的 1000/1020/1050 三对左图，各测试 75/150 点，共 675 个点，优化后的
坐标、有效标志和逐点迭代次数与原硬件实现逐项一致。六组 LK 平均
8.274 → 7.735 ms，约快 6.5%；这是独立 LK 的结果，不是完整 VIO 帧周期。
同组 8 点批次平均 9.075 ms、32 点批次 8.647 ms，最终保留 16 点。
曾尝试将双线性插值合为 2×2 depthwise convolution，收益较小且改变了
BF16 舍入结果，未纳入保留代码。原始实验在 `install/openvins-c906-frontend/lk-opt/`。

`build_tpu.py --lk-batch 16` 使 A53 的命令生成器与 C906 的 `C906_LK_BATCH=16`
匹配。批次开关仅作用于 LK 传输/命令实现，没有改变 FP64 后端类型或精度；
更换批次时应用和小核固件必须成对更换。

新增独立编译选项 `OPENVINS_FRONT_LATEST_INTRINSICS`，默认 OFF，通过
`build_tpu.py --frontend --latest-intrinsics` 编入。运行完整卸载时默认发布，
`OPENVINS_TPU_LATEST_INTRINSICS=0` 可关闭；CPU 默认入口不发布。
小核对应 `FRONT_LATEST_INTRINSICS`，由 `C906_FRONT_LATEST_INTRINSICS` 控制，
`--frontend-probe` 默认开启，其余固件默认关闭。

A53 在初始配置、后端更新完成以及成功的 ZUPT 后发布双目内参；数据包含模型、
尺寸及每相机八个原始 float64 参数，使用独立共享区中的双缓冲和版本号。
先写非活动缓冲、清理缓存，再发布活动索引。小核读取时使用当时已发布的
完整版本，不等待指定帧；若缓冲在读取期间被再次复用，则保留上次完整快照。
读取期间发生一次发布仍可读取先前已发布的完整值，不会读到一半新、一半旧。

当前几何检查仍在 A53，像素 LK 本身不使用内参。新增通道在小核像素处理结束时
取得快照，为后续几何迁移提供读取接口；这项没有消除现有特征点快照依赖。
CSV `detail_intrinsics_version` 记录小核实际取得的内参版本。
主机并发测试覆盖 200000 次发布及“写新缓冲时读取旧完整缓冲”，未出现混合参数：

```sh
g++-11 -O2 -std=c++14 -pthread \
  scripts/vio-compare/probes/c906-frontend/intrinsics_test.cpp \
  -o /tmp/front-intrinsics-test
/tmp/front-intrinsics-test
```

同一 FPV RAM 窗口（1002–1037 帧）的完整流水线对照，原始 FP64 后端、150 点：

| 指标 | 寻址优化关闭 | 寻址优化开启 |
| --- | ---: | ---: |
| LK 合计 | 17.50 ms | 16.71 ms |
| 小核前端实际计算 | 43.01 ms | 42.14 ms |
| 完整帧周期平均 | 58.14 ms | 57.41 ms |
| 吞吐 | 17.20 Hz | 17.42 Hz |
| 完整帧周期 P95 | 95.51 ms | 103.93 ms |

整帧平均快约 1.3%，P95 没有改善，不能据此宣称稳定 20 Hz；几何检查和
FP64 后端仍存在长尾。对照使用同一应用，固件只切换 `LK_TPU_FAST_ROI`，
内参快照通道两边均开启。优化版完整 1970 帧回放、1781 帧初始化后更新，
位姿与上一版硬件参考逐行一致；两组小核内参版本均从 1 开始、单调增长，
完整回放最终读到版本 1785。本机完整 1970 帧回放也与原 CPU 参考逐行一致。

`camera` trace 从完整调用入口计算帧周期；不使用消费等待入口移动来制造收益。
原始数据及汇总在 `install/openvins-c906-frontend/lk-opt/replay/`：

```sh
python3 scripts/vio-compare/openvins/compare_front_kernels.py \
  install/openvins-c906-frontend/lk-opt/replay \
  --reference install/openvins-c906-frontend/opt2/board/production.csv \
  --modes control final
```

最终设备实时冒烟测试 100 帧，63 帧初始化后更新，小核内参版本 1–63 单调增长，
确认静止 ZUPT 提前返回路径也会发布最新参数。摄像头 19.95 Hz、IMU 199.54 Hz，
初始化前丢 2 帧，初始化后没有新增 sequence gap；短静止测试不代表强运动吞吐。
普通固件及 Linux TPU 驱动已在退出后恢复。最新版本部署于
`/mnt/data/openvins-c906-front/live`，用前面的 `run-front.sh` 命令运行。


## LK 有效通道与窗口复用诊断

`C906_LK_PROFILE_OCCUPANCY=ON scripts/c906/build.sh --frontend-probe` 启用
`LK_TPU_PROFILE_OCCUPANCY`，默认关闭。遥测追加在 LK v3 控制结构尾部，
原字段偏移不变。独立 `c906-tpu-lk` 测试输出 `OCCUPANCY`：迭代提交数、
固定通道槽位、实际活跃槽位、所有活跃点 ROI 地址均未变化的提交数。
这些是逻辑计数，不用带诊断固件的时间作为生产版本速度结论。

同三对 FPV 左图各 75/150 点的六组测试，共 1832 次迭代提交：

* 29312 个固定通道槽位中，13428 个活跃，利用率约 45.8%（各组 40.1%–52.9%）。
* 956 次提交的所有活跃点整数 ROI 地址均不变，占 52.2%。
* 活跃点槽位中，约 60.2% 的整数 ROI 地址与上次相同。
* 675 个点的坐标、有效标志和迭代次数与原硬件参考逐项一致。

这些计数用于选择下节的新图像窗口缓存实验；按当前真实收敛状态压紧活跃点、
尾部动态缩批尚未实现。旧模板及梯度已经驻留片上。原路径 `center()` 原地修改
窗口，缓存路径另外保存未修改的像素块，每次从该缓冲中心化到工作区，避免
重复减去 128。

上述统计来自六组独立 LK 测试，尚不是整段 VIO 的利用率或这两项优化的提速
结果。生产固件及已部署应用没有启用本项诊断；数据在
`install/openvins-c906-frontend/lk-occupancy/results/summary.json`。


## LK 新图像窗口缓存

独立宏 `LK_TPU_ROI_CACHE` 默认 0。A53 用
`build_tpu.py --frontend --lk-roi-cache` 编入；C906 对应
`TIDESENSE_C906_LK_ROI_CACHE`，CMake 默认 OFF，`build.sh --frontend-probe`
默认 ON，`C906_LK_ROI_CACHE=OFF` 可关闭。完整启用需应用和小核均开启。
编入后默认启用，启动命令前加 `OPENVINS_TPU_LK_ROI_CACHE=0` 可用同一应用和
固件回到普通路径。此环境变量在一次性构建 TPU 命令时读取，改变它需要重启应用。

缓存只复用同一批次、同一金字塔层中的当前图像 17×16 像素块。第一轮强制
加载；后续仅当所有活跃点的整数 ROI 地址均未变时跳过窗口 G2L，仍加载新的
亚像素权重、插值、计算残差与求解。新批次、新层均重新加载，不跨帧复用。
另加一个 TPU 片上缓冲保存未中心化像素，避免重复减去 128；旧模板、梯度、
点数、迭代上限、收敛规则以及 FP64 后端都保持原规则。

A53 在启动时多生成一份无窗口 G2L 的迭代命令，C906 根据实际 ROI 地址选择。
不会增加帧内 A53 通信，仍每帧一次邮箱请求。LK v3 原字段偏移不变，缓存能力
和命令长度追加在控制结构尾部；缓存作业使用独立保留区并校验与权重区的边界。
命令生成器运行时关闭缓存或固件未开启时，使用普通迭代路径。

六组独立 LK 测试（同三对 FPV 图像、75/150 点），675 点的坐标、有效标志和
迭代次数均与原硬件参考逐项一致。平均 7.92 → 7.02 ms，约快 11.3%；
该微测试使用占用率诊断，完整 VIO 对照已关闭诊断。

同一应用、同一固件，切换 `OPENVINS_TPU_LK_ROI_CACHE=0/1`，FPV RAM 窗口
1002–1037 帧，150 点、原始 FP64 后端：

| 指标 | 缓存关闭 | 缓存开启 |
| --- | ---: | ---: |
| LK 合计 | 16.81 ms | 15.14 ms |
| 小核前端实际计算 | 42.23 ms | 40.56 ms |
| 完整帧周期平均 | 57.58 ms | 56.45 ms |
| 吞吐 | 17.37 Hz | 17.71 Hz |
| 完整帧周期 P95 | 104.09 ms | 101.51 ms |
| A53 等待前端结果 | 9.34 ms | 8.41 ms |

LK 快约 9.9%，完整帧周期快约 2.0%，仍非稳定 20 Hz。缓存版完整 1970 帧，
1781 帧初始化后更新，位姿与优化前硬件参考逐行一致；全片命中
413933 次、重新加载 457442 次，命中率约 47.5%。窗口内每帧平均命中约 335 次。
CSV `detail_lk_roi_cache_hits/misses` 是实际缓存命中/加载次数，独立于旧的
A53 `tpu_hits`。CPU 模拟版也完成 1970 帧、轨迹与 CPU 参考逐行一致；
它验证接入，TPU 缓存本身的精度以板上测试为准。

原始数据及汇总在 `install/openvins-c906-frontend/lk-cache/`：

```sh
python3 scripts/vio-compare/openvins/compare_front_kernels.py \
  install/openvins-c906-frontend/lk-cache/replay \
  --reference install/openvins-c906-frontend/lk-opt/replay/final.csv \
  --modes off on
```

最新设备实时测试 100 帧，72 帧初始化后更新；缓存命中 13554 次、重新加载
9213 次，命中率约 59.5%。摄像头约 19.95 Hz、IMU 199.54 Hz，初始化前丢
12 帧，初始化后没有新增 sequence gap。初始化后 camera 调用平均 5.27 ms，
短静止/ZUPT 场景不用于评价强运动吞吐。退出后普通固件和 Linux TPU 已恢复。
缓存版已部署到 `/mnt/data/openvins-c906-front/live`。

### 双批次 TDMA/TIU 流水

独立宏 `LK_TPU_DUAL_BATCH`，CMake 的 `OPENVINS_TPU_LK_DUAL_BATCH` 和
`TIDESENSE_C906_LK_DUAL_BATCH` 均默认 OFF。应用用 `build_tpu.py --lk-dual`
编入，同时需要 `--frontend --lk-roi-cache --lk-batch 16`；固件用
`C906_LK_DUAL_BATCH=ON scripts/c906/build.sh --frontend-probe`。
编入后默认开启，运行时 `OPENVINS_TPU_LK_DUAL=0` 回到单批次缓存版；
`OPENVINS_TPU_LK_DUAL_PARALLEL=0` 保留双批次，但串行执行，供归因对照。
关闭 ROI 缓存也自动回到原单批次路径。

CV181x 有 8 个 NPU 通道，每通道 32 KiB SRAM。本实现两套独立 16 点状态，
每套 8768 字节，第二套从 16384 字节开始以避开同一 SRAM bank，总计使用
25152 字节／通道。Hessian 三项和 X/Y 残差分别暂存，避免延后回写时覆盖。
一个命令包按加载 A、计算 A/加载 B、计算 B/回写 A、回写 B 调度。
SDK `parallel_enable()` 重叠 TDMA 与 TIU；两项 TIU 计算仍共享同一引擎。

保留原来每 16 点的分组、独立收敛、迭代上限及算术次序。一个批次先收敛后，
另一个批次继续用单槽命令；两者各自判断窗口缓存命中。一次双目帧仍只有一次
A53→C906 请求，迭代期间不用 A53 参与。可选字段追加到 LK v3 元数据尾部，
旧字段偏移不变，经典命令也保留以支持运行时关闭。
`detail_lk_dual_packets` / `detail_lk_dual_solo_packets` 分别记录迭代命令包数
及其中只含一个有效批次的包数；开启 `FRONT_PROFILE` 时，
`detail_lk_submissions` 包含初始化和迭代提交。

六组 FPV 窗口（1000、1020、1050 帧，各 75/150 点）共 675 点，坐标、有效性、
迭代次数与单批次 TPU 参考逐点一致，每组 5 次调用并排除第一次：

| 版本 | 六组平均 LK | 150 点三组平均 LK |
| --- | ---: | ---: |
| 单批次，ROI 缓存 | 6.900 ms | 9.231 ms |
| 双批次，串行 | 6.942 ms | 9.088 ms |
| 双批次，TDMA/TIU 流水 | 6.517 ms | 8.496 ms |

流水相比单批次平均快 5.5%，150 点时快约 8.0%；75 点收益较小。
例如第 1000 帧 150 点，提交从 394 降到 234 次，LK 从 8.338 降到 7.666 ms。
串行合批平均没有收益，主要收益来自搬运与计算重叠。
原始数据在 `install/openvins-c906-frontend/lk-dual/results/`。

同一应用、固件和参数，FPV RAM 窗口 1002–1037 帧，关闭 micro-ROS、FP64 后端：

| 指标 | 单批次缓存版 | 双批次串行 | 双批次流水 |
| --- | ---: | ---: | ---: |
| LK 平均 | 15.409 ms | 15.579 ms | 14.669 ms |
| 前端实际计算（去除快照等待） | 40.822 ms | 41.000 ms | 40.085 ms |
| 完整帧周期平均 | 56.968 ms | 56.896 ms | 56.197 ms |
| 吞吐 | 17.55 Hz | 17.58 Hz | 17.79 Hz |
| A53 等待前端结果 | 8.203 ms | 8.445 ms | 8.039 ms |
| 完整帧周期 P95 | 101.88 ms | 103.40 ms | 101.45 ms |

完整 LK 快约 4.8%，帧周期快约 1.4%，尚非稳定 20 Hz；后端 CPU 平均均约
33.7 ms。测量区间平均每帧有 571 个迭代包，其中 339 个只有一个有效批次，
约 59%，因而可重叠部分有限。串行合批单独不作为加速方案。
流水版完整 1970 帧、1781 帧初始化后输出，位姿与上一版硬件参考逐行一致。
所有测量均保持 150 点配置、LK 16×15、原收敛条件和 FP64 后端；整段 SD 回放
wall 时间不用于评估计算吞吐。原始轨迹及 trace 在 `lk-dual/replay/`：

```sh
python3 scripts/vio-compare/openvins/compare_front_kernels.py \
  install/openvins-c906-frontend/lk-dual/replay \
  --reference install/openvins-c906-frontend/lk-cache/replay/on.csv \
  --modes baseline serial parallel
```

最终版本已成对部署到 `/mnt/data/openvins-c906-front/live` 和 `tools`，使用本页
原启动命令即可。实时验证 100 帧、64 帧初始化后输出，执行 26716 个迭代包；
3 次 sequence gap 均在初始化前，初始化后未新增丢帧。摄像头 19.95 Hz、
IMU 199.54 Hz，当前短静止/ZUPT 场景仅验证接入，不代表强运动吞吐。
退出后恢复普通固件及 Linux TPU 驱动；运行时关闭 ROI 缓存的回退路径亦已上板验证。

## 七点法 double QR 零空间求解

独立编译选项 `OPENVINS_FRONT_QR7POINT` 默认 OFF，通过
`build_tpu.py --frontend --qr7` 编入；已编入时默认启用，
`OPENVINS_QR7POINT=0` 可在同一程序中回到原 OpenCV SVD。
`OPENVINS_QR7POINT_FORCE_SVD=1` 是诊断选项：保留同一个自定义 callback，
但每次假设都调用原七点法，用来核对与原注册器的采样和输出一致性。

只替换七点法中 7×9 齐次方程的二维零空间求解。使用 double Householder QR，
对 QR 对角线比例、正交性、零空间残差、三次多项式和模型残差做检查，失败时
回退原 SVD。使用 OpenCV 的固定随机种子生成零向量方向，尽量保持原三次方程
的基底方向和模型输出顺序。普通 QR 基底的首轮试验仍能跟踪，但本机 ATE 从
0.2748 m 变为 0.2853 m；该版本没有部署，原始数据保存在 `qr7/host-plain/`。
最终基底对齐版本通过了完整轨迹逐行一致性检查。

随机采样、共线检查、误差定义、RANSAC 自适应终止继续使用本 SDK 的 OpenCV
3.4 注册器，阈值仍为 `2/max_focal_length`、置信度 0.999，上限保持原 1000。
少于 15 对应点时沿用原接口的 LMeDS 路径；现有 RANSAC-200 选项没有开启。
后端保持原始 FP64，点数、LK 阈值、噪声和初始化参数没有改变。
实现绑定 SDK 的 OpenCV 3.4 私有 callback 接口，编译时检查版本；移植到其他
版本需重新核对 ABI。OpenCV BSD 声明见 `probes/qr7point/NOTICE`，实时包附带
`NOTICE-OpenCV-QR7.txt`。

### 精度与耗时

1000 组七点样本、40 组 RANSAC 对照及退化样本通过；强制 SVD 路径与原接口
输出一致。A53 的 QR/SVD 模型集合及输出顺序最大归一化差约 7.3e-12。
主机 ASan/UBSan 检查通过，编译时关闭宏的分支也通过检查。

本机完整 1970 帧、1781 帧初始化后位姿与关闭优化逐行一致，ATE 为 0.2748 m。
A53 完整 1970 帧同样与上一版硬件参考逐行一致，ATE 保持 0.2867 m。
ATE 均使用相同的 816 个真值覆盖时刻，应用官方 -16.684572 ms 时间偏移，
SE(3) 对齐，不拟合尺度。A53 运行 119227 次假设，均走 QR；退化回退由单元
测试覆盖。

SG2002 A53 1 GHz、C906 750 MHz，关闭 ROS、150 点、相同 FPV RAM 窗口
1002–1037 帧，同一程序运行时切换：

| 指标 | 原 SVD | double QR |
| --- | ---: | ---: |
| 几何／维护及相关同步平均 | 13.754 ms | 9.330 ms |
| C906＋TPU 前端实际计算 | 40.073 ms | 40.078 ms |
| 前端＋几何维护平均预算 | 53.827 ms | 49.407 ms |
| 前端＋几何维护 P95 | 81.824 ms | 69.774 ms |
| 完整帧周期平均 | 56.177 ms | 53.374 ms |
| 完整帧周期 P95 | 101.283 ms | 86.774 ms |
| 吞吐 | 17.80 Hz | 18.74 Hz |

几何平均快约 32%，完整帧周期快约 5%。前端本身没有加速，精度与特征集合保持
一致。两项平均预算已低于 50 ms，但该窗口约 47% 的帧两项之和仍超过 50 ms，
尚非逐帧达标或稳定 20 Hz；FP64 后端也仍有长尾。此处几何区间从当前前端结果
返回到后端开始，包含维护、共享快照及相关调度；前端和几何原本部分重叠，
两项之和仅是预算指标，实际吞吐以 camera 入口周期为准。

原始数据位于 `install/openvins-c906-frontend/qr7/`。核对命令：

```sh
python3 scripts/vio-compare/probes/qr7point/build_test.py --arch host
install/openvins-c906-frontend/qr7/qr7_test-host force
install/openvins-c906-frontend/qr7/qr7_test-host
python3 scripts/vio-compare/probes/qr7point/build_test.py --sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  install/openvins-c906-frontend/qr7/qr7_test-host-asan
python3 scripts/vio-compare/probes/qr7point/evaluate.py \
  install/openvins-c906-frontend/qr7/host
python3 scripts/vio-compare/probes/qr7point/evaluate.py \
  install/openvins-c906-frontend/qr7/board --modes qr \
  --reference install/openvins-c906-frontend/lk-dual/replay/parallel.csv
```

最终版本已部署到 `/mnt/data/openvins-c906-front/live`，原启动命令不变。
源码编译开关仍默认 OFF，部署构建显式使用 `--qr7`。同一二进制切回原实现：

```sh
cd /mnt/data/openvins-c906-front/live
OPENVINS_QR7POINT=0 OPENVINS_MICRO_ROS=0 ./run-front.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-svd.csv 0
```

实时开关对照各 100 帧，均有 61 帧初始化后输出；两者在初始化切换附近均出现
1 次 sequence gap。QR 复测执行 426 次模型求解，全部走 QR，退出后恢复普通
固件及 Linux TPU 驱动。首轮高增益画面下未初始化，没有用于新旧性能比较；
该测试没有调整曝光和标定参数。实时短静止/ZUPT 测试用于验证接入，强运动
吞吐和轨迹精度以前述 FPV 回放为准。

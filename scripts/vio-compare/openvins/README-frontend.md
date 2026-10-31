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

## A53 空闲时领取 16 点尾部 LK（实验）

应用编译选项 `OPENVINS_FRONT_CPU_TAIL`、小核选项
`TIDESENSE_C906_FRONT_CPU_TAIL` 均默认 OFF。编入后还需显式设置
`OPENVINS_TPU_CPU_TAIL=1`；不设置或设为 0 时继续由 C906／TPU 完成全部点。
本实验没有更改逐相机几何处理，也没有恢复 A53 直方图实验。

应用在第一轮有效后端执行之后，才允许在消费前端结果的等待区间声明空闲。
C906 在开始右路时序 LK 时，如果 A53 已声明空闲、该路至少有 48 个点，
便交出最后 16 点，并继续计算其余点。每帧最多交接一次；点少或 A53 仍忙时
全部按原路径完成。初始化阶段保留原路径，后端计算期间不领取任务。

A53 复用同一对共享灰度金字塔，仅计算所领窗口的 Scharr，使用原 NEON LK。
CPU 计算完成后由 C906 按原索引合并，点的 ID 和顺序保持不变。
任务、结果以及两侧控制状态使用独立缓存行，一侧只写自己拥有的区域；
通过 cache clean／invalidate 发布，不使用跨处理器 compare/exchange。
协作区位于共享 ION 偏移 1024000，长度 1024 字节，不改变前端 v3 原字段。
单帧仍是一条邮箱请求，CPU／TPU 迭代中间无需 A53 串接提交。

配对固件会确认协议。应用开启实验但固件不支持时明确报错；CPU 出错或失去
响应时，小核分别返回 -71／-72，等待上限 500 ms。独立的模拟缓存测试覆盖
无空闲声明、点数不足、重复领取、成功合并、CPU 失败及超时。
启动时在 CPU 私有图像上预热 LK 和工作区，减少首次调用的开销；代码页后续仍
可能被回收，锁页对照见下文。没有在运行中重建整图梯度。后端保持 FP64，150 点、LK 16×15、30 次
迭代上限、噪声及 RANSAC 参数沿用原设置。

构建实验配对程序：

```sh
C906_FRONT_CPU_TAIL=ON C906_LK_DUAL_BATCH=ON \
  scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/openvins/build_tpu.py \
  --frontend --early-pipeline --sparse-nms --lazy-mask --latest-intrinsics \
  --lk-batch 16 --lk-roi-cache --lk-dual --qr7 --cpu-tail --lock-exec --arch a53
```

实验部署目录为 `/mnt/data/openvins-c906-front/cpu-tail`，正式 `live`、`tools`
仍是原版本。回放命令：

```sh
exp=/mnt/data/openvins-c906-front/cpu-tail
C906_TOOLS_DIR="$exp/tools" OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_CPU_TAIL=1 \
  OPENVINS_TPU_LOCK_EXEC=1 \
  /mnt/data/openvins-c906-front/live/with-tpu.sh "$exp/run_tail-lock.a53" \
  /mnt/data/openvins-c906-tpu/config-fpv/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset "$exp/results/manual.csv" 1970 0
```

`front_tail_jobs` 是本帧交接批数（0／1），每批 16 点；`front_tail_ms`／
`front_tail_cpu_ms` 是 CPU 任务 wall／CPU 时间，`front_tail_sync_ms` 是任务和
共享灰度读取同步，`front_tail_remote_wait_ms` 是 C906 在本地工作完成后等待
CPU 的时间。`front_total_ms` 包含这段合并等待；`consume_wait` 区间现在也
包含 CPU 任务，不能将整个区间都当作 A53 空闲时间。

### 锁定可执行文件映射的独立开关

单次预热不足以保证 CPU 协作代码一直驻留。未锁页的完整回放中，少数任务的
CPU 时间约 4–5 ms，wall 时间却达到 43–44 ms。`perf record -e major-faults`
在同一个任务区间捕获了 `cv::Mat`、`cv::calcOpticalFlowPyrLK` 的主缺页，
确认有从 SD 重新读入代码页的阻塞。不能将这些尖峰从实机表现中忽略。

为此新增独立编译开关 `OPENVINS_FRONT_LOCK_EXEC`，默认 OFF；
`build_tpu.py --lock-exec` 编入。运行时显式设 `OPENVINS_TPU_LOCK_EXEC=1`，
仅锁定 `/proc/self/exe` 对应的文件映射，至进程退出自动释放，不锁整堆或未来
分配。测试程序锁定 12480512 字节，`VmLck=12188 kB`，约 11.9 MiB。
失败会明确报错；本板 root 运行已通过。该开关可独立于分流开启，正式包未启用。

锁页对照必须让“不分流”和“分流”两组都设 `OPENVINS_TPU_LOCK_EXEC=1`，
避免把锁页效果误算为分流收益。原 `run_tail.a53` 留作未锁页实验的原始程序，
支持两个独立开关的新版是 `run_tail-lock.a53`。

### 精度及未锁页重复对照

两轮完整开启回放均处理 1970 帧、初始化后输出 1781 帧，ATE 分别为
0.298629／0.298635 m，原 TPU 基线为 0.286721 m。使用相同的 816 个真值时刻、
官方 -16.684572 ms 时间偏移、SE(3) 对齐，不拟合尺度。首轮相对基线的最大
位置差约 0.178 m、末帧差约 0.141 m，最大姿态差约 0.254°，未见发散。
GT 只覆盖录像的一部分；GT 范围外的比较是相对基线，不能当作绝对精度。

混合 CPU／TPU 路径有舍入和收敛差异，结果不是逐位等价。两轮完整回放虽然
ATE 接近，参与分流的帧仍可能因运行时空闲时机而不同。关闭开关的对照与原
硬件参考逐行一致。没有修改噪声、视觉阈值、初始化或协方差精度来补偿差异。

同一 FPV RAM 窗口 1002–1037 帧，按 camera 入口计算帧周期，两轮未锁页对照：

| 项目 | 首轮关闭 | 首轮开启 | 复测关闭 | 复测开启 |
| --- | ---: | ---: | ---: | ---: |
| 平均完整帧周期 | 53.137 ms | 52.270 ms | 53.721 ms | 53.252 ms |
| 完整帧周期 P95 | 87.322 ms | 81.588 ms | 88.460 ms | 85.134 ms |
| 窗口内 CPU 批数 | 0 | 16 | 0 | 19 |

相邻配对平均节省 0.47–0.87 ms，约 0.9%–1.6%，收益较小且有调度波动。
整段 SD 回放 wall 时间不用于评价计算吞吐。锁页后的配对结果见下文。

验证协议与分析精度：

```sh
g++-11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  scripts/vio-compare/probes/c906-frontend/tail_protocol_test.cpp \
  -o /tmp/tidesense-tail-protocol-test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  /tmp/tidesense-tail-protocol-test
python3 scripts/vio-compare/probes/qr7point/evaluate.py \
  install/openvins-c906-frontend/cpu-tail/results \
  --modes on on-full-repeat locked-on \
  --reference install/openvins-c906-frontend/qr7/board/qr.csv
```

原始轨迹、逐帧指标、trace、缺页采样及汇总保存在
`install/openvins-c906-frontend/cpu-tail/` 和设备同名实验目录。
主机 CPU 模拟仍走原路径；不能用该模拟回放替代板上混合计算的精度验证。

### 锁页后配对结果

同一 `run_tail-lock.a53` 和同一小核固件，两组均锁页。下表使用不带 perf 的
`locked-off`／`locked-on-clean`，相同 36 个强运动帧周期：

| 指标 | 不分流 | A53 最多领取 16 点 |
| --- | ---: | ---: |
| 平均完整帧周期 | 53.777 ms | 51.876 ms |
| 帧周期 P95 | 88.253 ms | 81.556 ms |
| 平均吞吐 | 18.60 Hz | 19.28 Hz |
| 测量窗口内交接批数 | 0 | 16 |

这一对平均节省 1.90 ms（3.5%），仍不能稳定达到 20 Hz。未锁页重复对照的
收益较小，不能只用最好的一轮推断所有场景。任务归属会随调度时机变化，
CPU／TPU 数值差异也会改变观测集合；这里是整个实现的对比，不是逐位等价的
纯调度加速。继续保留实验开关，不改正式启动默认值。

不带 perf 的开启复测共运行 1040 帧、交接 18 批，CPU 每批 wall 平均
3.445 ms、P95 4.485 ms、最大 4.751 ms；缓存同步平均约 0.103 ms。
小核在交接帧等待 CPU 平均 0.156 ms、最大 0.950 ms，大多数计算已重叠。
这些是当前数据样本，不是最坏执行时间保证。

另一次完整锁页回放处理 1970 帧、初始化后输出 1781 帧，交接 19 批，其中
18 批在测量窗口；ATE 为 0.294956 m，原参考为 0.286721 m。
该轮带 major-faults 诊断采样，仅用于精度、长延迟及缺页检查，不作为上表的
速度对照。所有交接批次 wall 最大 4.329 ms，未再出现原来的 43–44 ms 尖峰；
运行中采样没有记录到 major fault。初始化锁页本身的预取不算在该采样中。

诊断采样的 perf 在目标线程退出后出现大量唤醒，因此正式速度结论另外使用
不带 perf 的复测。关闭分流的两个未锁页对照和锁页对照均与旧硬件参考逐行一致。
编译关闭分流／锁页的分支检查通过；旧固件不支持分流时的明确报错也已实测。

关键结果文件：`results/summary.json` 为全部轮次指标，`results/accuracy.json`
为完整回放精度；`results/locked-on-clean*` 为无 perf 复测，
`results/major-faults*`、`results/locked-major-faults*` 为缺页诊断。

## 长尾帧定位与 FP64 矩阵打包

上述 88.253 ms 是第 1002–1037 帧这个 36 帧窗口的 P95，不是整段数据集的
最大帧耗时。窗口最慢约 98 ms；完整 1970 帧里还有更重的后端帧。
测试数据仍为 UZH-FPV `indoor_forward_6`，A53 1 GHz、150 点、完整 FP64。

OpenVINS 内部的 SLAM 特征指保留在滤波器状态中的长期三维特征点，用于
局部视觉约束。这仍是纯 VIO，没有启用回环、重定位或全局建图。
当前配置 `max_slam: 50`，`max_slam_in_update: 25` 是每批更新上限；一帧
可以分多批更新。丢失或失效的长期点会被清理，新点经过三角化、检验和状态
增广后加入。这里的 delayed initialization 是这一过程，与回环无关。

保留前缀初始化的逐帧诊断发现两类慢帧，帧号从 0 开始：

| 帧 | 慢的原因 | 细分诊断 |
| --- | --- | --- |
| 1004、1005、1018 | 同帧初始化 4 个长期点，连同常规更新共执行 6 次 EKF 更新 | 延迟初始化约 38–47 ms；全部 EKF 的 `P Hᵀ` 约 19–21 ms |
| 1025 | 两路几何内点只有 24/40、25/43 | RANSAC 尝试 243＋304 组模型，约 33 ms |
| 1030 | 两路几何内点只有 29/52、34/52 | RANSAC 尝试 408＋253 组模型，约 39 ms |

画面是仓库内倾斜、回正的一段，地面占比较大，不能仅凭角速度判断计算量。
`install/openvins-c906-frontend/tail-latency/worst-frames.png` 保存了对应双目
图像；图上的角速度为相机原始时间戳附近 ±25 ms 内的峰值，未修正在线时差。
嵌套的初始化、EKF 和矩阵计时有包含关系，不能直接相加。

新增独立开关 `OPENVINS_EKF_PACKED_F64`，默认 OFF；构建时给
`build_tpu.py` 增加 `--fp64-pack`。编入后仍需显式设置
`OPENVINS_EKF_PACKED_F64=1` 才启用。它按 Jacobian 的变量顺序，打包相应的
协方差列，用一次 FP64 矩阵乘法替代大量小块乘法来形成 `M_a = P Hᵀ`。
状态、协方差、增益、残差求解和写回均为 double；点数、噪声、门限、RANSAC
上限及滤波更新顺序保持原设置。没有引入完整或局部 FP32。

`OPENVINS_BACKEND_DETAIL` 是独立诊断编译宏，默认 OFF，对应
`build_tpu.py --backend-detail`。运行时设置 `OPENVINS_BACKEND_DETAIL_FILE`
才记录阶段与矩阵维度；`OPENVINS_BACKEND_DETAIL_FIRST/LAST` 按相机时间戳
限制诊断范围。`OPENVINS_QR7_DETAIL_FILE` 另记每次 RANSAC 的点数、内点、
模型组数和时间。速度对照不启用这些逐次诊断，也不运行 perf。

完整 1970 帧回放、1781 帧初始化后输出，FP64 打包的板上 ATE 为
0.2867215 m，原参考 0.2867214 m；全段未对齐的位置差最大约 0.0000027 m。
主机回放 ATE 为 0.274340 m，原参考 0.274801 m。共同真值仍只覆盖 816 帧，
SE(3) 对齐、不拟合尺度；这不能证明所有场景数值完全相同。

另测了完整主元 LU 替代七点 QR 的原型：主机数值检查通过，完整轨迹与参考
一致；A53 的窗口几何及附带准备 CPU 平均仅从 8.096 变为 8.064 ms，
没有实质收益，因此没有接入维护的求解器。原型与原始对照留在上述实验目录。

### 内存记录复测

最终对照将 pose、trace 和运行日志先写入 `/tmp`，退出后再移到 SD；测试
窗口预载 40 对图像，运行期间不拉取文件，不运行 perf。早期写 SD 的对照中
出现过 wall 比线程 CPU 多出 30–60 ms 的停顿，不能把它归因于求解器变慢。
下表均使用同一二进制、同一固件并锁页，窗口仍为 1002–1037 帧：

| 指标 | 原计算，不分流 | FP64 打包，不分流 | FP64 打包＋16 点分流 |
| --- | ---: | ---: | ---: |
| 平均完整帧周期 | 53.250 ms | 51.060 ms | 50.532 ms |
| 帧周期 P95 | 87.199 ms | 87.982 ms | 79.029 ms |
| 窗口最大帧周期 | 97.662 ms | 91.292 ms | 103.697 ms |
| 平均吞吐 | 18.78 Hz | 19.58 Hz | 19.79 Hz |
| 后端 wall 平均 | 33.754 ms | 29.244 ms | 28.989 ms |
| 超过 50 ms 的帧数／36 | 19 | 16 | 18 |

单独打包将第 1018 帧后端 CPU 从 73.425 降到 64.499 ms，但帧周期只减少
约 6.4 ms：前一帧后端变快后，会多等一部分下一帧前端。它降低了后端峰值，
尚未改善 RANSAC 主导的 P95。组合分流后 P95 下降，最大值却没有改善，
不能宣称已控制最坏执行时间或保证每帧 50 ms。

组合版本完整回放 1970 帧、初始化后 1781 帧，ATE 0.289410 m；共交接
25 批，其中窗口内 22 批，CPU 批次最大 4.366 ms。CPU／TPU 数值不同且
任务归属随调度变化，会改变观测集合；组合结果不是逐位等价的纯矩阵加速。
正式程序及启动配置没有替换，继续保留可选开关。

构建时在前述选项基础上增加 `--fp64-pack`，例如：

```sh
python3 scripts/vio-compare/openvins/build_tpu.py \
  --frontend --early-pipeline --sparse-nms --lazy-mask --latest-intrinsics \
  --lk-batch 16 --lk-roi-cache --lk-dual --qr7 --cpu-tail --lock-exec \
  --fp64-pack --arch a53 --jobs 4
```

本轮设备实验二进制位于 `tail-latency/run_tail_latency.a53`。单独启用打包的
RAM 窗口复现（先确保没有其他摄像头／TPU 使用者）：

```sh
cd /mnt/data/openvins-c906-front
mkdir -p /tmp/ov-tail-repro
unset OPENVINS_BACKEND_DETAIL_FILE OPENVINS_QR7_DETAIL_FILE
C906_TOOLS_DIR="$PWD/cpu-tail/tools" OPENVINS_TPU_FRONTEND=1 \
  OPENVINS_TPU_CPU_TAIL=0 OPENVINS_TPU_LOCK_EXEC=1 OPENVINS_EKF_PACKED_F64=1 \
  OPENVINS_PRELOAD_FIRST=1000 OPENVINS_PRELOAD_COUNT=40 \
  OPENVINS_TPU_TRACE=/tmp/ov-tail-repro/packed-trace.csv \
  ./live/with-tpu.sh ./tail-latency/run_tail_latency.a53 \
  ./tail-latency/config/speed/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset /tmp/ov-tail-repro/packed.csv 1040 0 \
  >/tmp/ov-tail-repro/packed.log 2>&1
```

原始文件、构建信息和逐帧拆分保存在本机
`install/openvins-c906-frontend/tail-latency/`；`ram-*` 为最终速度对照，
`detail-*`／`qr-detail.csv` 为单独诊断。可复算：

```sh
python3 scripts/vio-compare/openvins/analyse_tail.py \
  install/openvins-c906-frontend/tail-latency --modes ram-off ram-pack ram-tail-pack
python3 scripts/vio-compare/probes/qr7point/evaluate.py \
  install/openvins-c906-frontend/tail-latency --modes pack-on ram-tail-pack \
  --reference install/openvins-c906-frontend/qr7/board/qr.csv
```

## IVE 候选方案与独立验证（2026-10-30）

本节记录独立验证及 OpenVINS 实验接入。Linux 调用硬件 IVE、C906 直接控制和
完整前端回调均已实现。C906 实验**没有修改 Linux 设备树、内核镜像或 SD 启动文件**。
板上存在 `/dev/cvi-ive`，`cv181x_ive` 已加载，IVE 时钟 300 MHz。
SDK 的 `cvi_mpi/modules/ive` 通过 ioctl 调用该独立引擎，寄存器驱动在
`osdrv/interdrv/ive/hal/cv181x/cvi_ive_platform.c`。它与主要复用 TPU 的
`ive/` 图像库是不同路径；后者的 Hist／EqualizeHist 在当前源码中还是 CPU
实现，不能仅凭 IVE 函数名判断加速或并行关系。

### 已测：直方图与 LUT

独立探针使用共享物理内存，输入提前填入并 clean cache；计时包含双目两次
ioctl、完成等待及输出 cache invalidate，排除分配、输入填充和结果校验。
测试随机灰度、两段常值灰度两种输入，每个尺寸／模式预热一次、计时 50 次。
每次检查全部 256 个直方图桶，Map 检查所有输出像素；均与 CPU 参考一致。
这不是 C906 实测，也未包含 IVE 与 TPU 同时工作时的总线争用。

| 双目输入 | 模式 | Hist wall／A53 CPU | Map wall／A53 CPU |
| --- | --- | ---: | ---: |
| 640×400 | 轮询 `bInstant=1` | 1.883／1.882 ms | 约 2.28–2.37／2.28 ms |
| 640×400 | 中断 `bInstant=0` | 1.904／0.182 ms | 2.35／0.525 ms |
| 640×480 | 轮询 `bInstant=1` | 2.231／约 2.23 ms | 约 2.65–2.66／2.64–2.65 ms |
| 640×480 | 中断 `bInstant=0` | 2.247／0.182 ms | 约 2.71–2.72／0.53 ms |

SDK 头文件将 `bInstant` 写成 dummy，但当前驱动实际区分两条路径：1 在
`cvi_ive_go()` 中 `udelay(10)` 轮询；0 开中断并等待 completion。因此 Linux
路径也不必烧掉整个硬件运行时间的 A53 CPU，中断模式实测仅约 0.18 ms／双目。
探针直接检查 ioctl 返回值，避免 SDK 包装函数吞掉 ioctl 错误而误判成功。
原始源码、输出和运行日志保存在 `install/openvins-c906-frontend/ive-check/`，
设备探针在 `/mnt/data/openvins-c906-front/ive-check-20261030/probe.a53`：

```sh
# 参数 1 为轮询，0 为中断等待；仅在 IVE 没有其他使用者时运行。
timeout 25 /mnt/data/openvins-c906-front/ive-check-20261030/probe.a53 0
```

此前带细分计时的同一 FPV RAM 窗口，C906 双目直方图及 LUT 生成约 7.10 ms，
TPU 应用 LUT 约 0.76 ms。优先尝试 **IVE Hist＋C906 生成 LUT＋TPU 应用 LUT**。
7.10−2.25≈4.85 ms 只是这部分的理论时间差，还要扣除 LUT 生成、缓存同步及
接入成本；不能直接当作整帧收益。Map 当前比 TPU 路径慢，暂不迁移。

### 已测：不修改设备树的 C906 直控

源码及复现命令见 [`../probes/c906-ive/README.md`](../probes/c906-ive/README.md)。
启动时临时卸载 Linux IVE 模块，桥接模块以 `ive=1` 预留现有
`a0a0000.ive` 的 MMIO 区域，C906 独占执行。退出后恢复原小核固件与 Linux
TPU／IVE 驱动。现有时钟提供者已经将 `clk_ive_vip` 标为 `CLK_IS_CRITICAL`，
无需调整设备树、增加 reserved-memory 或更改共享 VIP 复位。

A53 只在启动时用 SDK 原有分块逻辑生成固定物理缓冲的寄存器表；每次作业由
C906 写寄存器并检查完成、超时。批内无 A53 阶段性提交。固件的
`C906_FRONT_IVE_HIST`、`C906_FRONT_IVE_ST` 均默认关闭。下文的完整流水线实验
需要程序、固件和桥接模块配套，单独在旧的正式程序上设置环境变量不能启用。

640×400／480 各测试常值、直边、8 像素棋盘格、固定种子随机灰度及三张
UZH-FPV `indoor_forward_6` 实图，共 14 组输入。每组先用 Linux 驱动生成两次
ST 参考并逐字节比较，再测 C906 Hist、ST、带非对齐输入整理的 ST，各连续
20 次。42 组计时全部完成；每组结束后检查完整 256 桶或全部输出像素及 DMA
保护区，均无错误。ST 参考仅证明与 SDK 硬件路径一致，不代表与 OpenCV 检测器
等价或 VIO 精度已经验证。

| 单图输入 | Hist | ST，已对齐输入 | 整理输入＋ST | 其中整理及缓存同步 |
| --- | ---: | ---: | ---: | ---: |
| 640×400 | 0.936 ms | 2.067 ms | 3.026 ms | 0.958 ms |
| 640×480 | 1.107 ms | 2.452 ms | 3.602 ms | 1.150 ms |

表中为 C906 计时的各组均值；640×480 三种路径的最大单次分别为
1.109／2.453／3.606 ms。排除一次性生成、分配、主机填充和末尾校验；
整理路径包含每次小核拷贝及输入 cache invalidate／目标 clean。一次 mailbox
等待 20 个作业，A53 CPU 合计约 0.04–0.06 ms；这不是每帧 OpenVINS 开销。
日志和解析结果保存在 `install/openvins-c906-frontend/ive-c906/suite/`。

原来的金字塔带 18 像素边框，640 宽时 stride=676、像素起点非对齐，不能直接
交给 ST。初试该布局会超时，Linux 同布局也异常；使用独立的对齐灰度缓冲后
通过校验。探针的小核整理使用对齐 64 位加载拼接，避免 C906 非对齐访存。
这个独立探针的整理成本为约 1.15 ms／图。完整前端实验改用上游直接输出对齐
灰度的方案，不执行这次整理。IVE 与 TPU 的异步并发提交尚未实现。

### 完整 C906＋TPU＋IVE 前端实验

对齐开关为 `OPENVINS_FRONT_ALIGNED_IMAGES`／`C906_FRONT_ALIGNED_IMAGES`，默认
关闭。支持 **640×400 和 640×480**，保持每侧 18 像素 REFLECT_101 边框，
仅调整 row stride 和 padded plane 的起点。首层两种尺寸均为 stride=688，
有效图像地址及每行起点都按 16 字节对齐。TPU LUT 直接写入该布局，后续 TPU
金字塔、LK、A53 尾部分流的 Mat 视图都使用实际 stride，不产生 ST 整理拷贝。
每个 camera slot 的五层金字塔分别占 431,296／506,880 字节，仍小于既有
655,360 字节的 slot，不增加小核 reserved-memory。新布局前端版本为 4，
旧布局为 3；程序和固件不匹配会在执行前端时拒绝请求。

两种尺寸各用 6 对 FPV 图像上板检查：五层有效像素、完整 18 像素边框、
特征跟踪与参考一致；直接对 TPU 输出运行 IVE Hist、ST，直方图全部桶正确，
ST 与独立连续缓冲参考逐像素一致，`pack_ticks=0`。直接读取 stride=688 的
ST 单图约 2.061 ms（400 高）／2.444 ms（480 高）。这是布局／算子验证；
完整流水线收益必须另测。

完整前端按独立宏接入以下回调，均默认关闭：

| 功能 | A53 编译选项 | 固件环境变量 | 运行开关 |
| --- | --- | --- | --- |
| 对齐灰度 | `--aligned-images` | `C906_FRONT_ALIGNED_IMAGES=ON` | 编译时决定，程序与固件必须一致 |
| IVE 直方图 | `--ive-hist` | `C906_FRONT_IVE_HIST=ON` | `OPENVINS_TPU_IVE_HIST=1` |
| IVE ST 候选图 | `--ive-st` | `C906_FRONT_IVE_ST=ON` | `OPENVINS_TPU_IVE_ST=1` |

Hist 使用原始连续灰度，C906 从硬件 256 桶沿用原 CDF、常值图和舍入规则，TPU
继续应用 LUT。ST 仅在需要补点时读取当前检测源 slot 的对齐灰度；硬件输出
U8 响应图，C906 做局部最大值筛选、网格选点、掩码、间距检查和原亚像素细化。
ST 改变候选定义和点集，与 FAST 路径不等价。`OPENVINS_TPU_IVE_QUALITY` 为
1..255 的硬件质量阈值；程序和对照脚本缺省均为 **16**。初轮使用 3，后续按
实际点数调到 16，结果见下文。原 FAST threshold 不是这个参数。
首轮只测吞吐；后续已补充该 FPV 序列的完整精度对照，结果见下文。
ST 功能本身仍默认关闭。

A53 在启动时生成两个 Hist 和四个 ST 固定缓冲寄存器表，每帧仍只提交一次完整
前端。IVE 回调不额外往返 A53；完整 IVE 请求使用桥接 ioctl 6，要求桥接模块
已经以 `ive=1` 独占 IVE，否则拒绝执行。固件能力和实际调用次数均检查，
CSV 中增加 `front_ive_histogram_calls`／`front_ive_st_calls` 及硬件作业时间。
`front_detect_ms` 才包含 ST 响应生成、软件选点和细化的完整耗时。

```sh
# 仓库根目录：编译配套固件与 FP64 程序。
C906_OUT="$PWD/install/openvins-c906-frontend/aligned-images/tools" \
C906_FRONT_ALIGNED_IMAGES=ON C906_FRONT_IVE_HIST=ON C906_FRONT_IVE_ST=ON \
C906_LK_DUAL_BATCH=ON C906_FRONT_PROFILE=ON \
scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/openvins/build_tpu.py \
  --frontend --early-pipeline --sparse-nms --lazy-mask --latest-intrinsics \
  --lk-batch 16 --lk-roi-cache --lk-dual --qr7 --cpu-tail --lock-exec \
  --fp64-pack --backend-detail --aligned-images --ive-hist --ive-st --arch a53

# 设备独立实验目录：配好 run_ive.a53、with-tpu.sh、benchmark_ive.sh、tools/。
# tools/ 中保留原 cvirtos-reload-base.bin，退出恢复用。
cd /mnt/data/openvins-c906-front/aligned-images
./benchmark_ive.sh \
  /mnt/data/openvins-c906-front/tail-latency/config/speed/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset results 1040
```

`benchmark_ive.sh` 对同一程序／固件切换 off、hist、st、both，保持 FP64 后端、
packed FP64 优化、关闭 CPU 尾部分流，预载第 1000 帧起 40 对图到 RAM。
日志和 CSV 先写 `/tmp`，每次完成才移到输出目录。前端统计排除 snapshot
等待，整体周期从 camera trace 计算，分别报告均值、P95 和吞吐；同时记录
左右特征数量，防止把少处理点造成的变快误认为等工作量收益。

```sh
# 将 results/ 取回本机后，分析同一 RAM 窗口。
python3 scripts/vio-compare/openvins/analyse_ive.py \
  install/openvins-c906-frontend/aligned-images/results
```

首轮（ST quality=3）完整流水线实测：A53=1 GHz、IVE=300 MHz、TPU=900 MHz，
UZH-FPV `indoor_forward_6` 的 640×480 双目，运行前 1040 帧，统计 RAM 窗口
1002..1037。四组使用相同 FP64 程序和小核固件，仅改变 IVE 运行开关。

| 模式 | 前端有效时间均值 | 整体周期均值 | 周期 P95 | 吞吐 | 左／右点数均值 |
| --- | ---: | ---: | ---: | ---: | ---: |
| C906＋TPU，IVE 关闭 | 40.489 ms | 51.508 ms | 88.300 ms | 19.41 FPS | 49.8／54.5 |
| 加 IVE Hist | 35.724 ms | 48.392 ms | 82.761 ms | 20.66 FPS | 49.8／54.5 |
| 加 IVE ST | 46.586 ms | 72.308 ms | 143.544 ms | 13.83 FPS | 73.3／66.5 |
| 加 IVE Hist＋ST | 41.801 ms | 70.439 ms | 138.940 ms | 14.20 FPS | 73.3／66.5 |

Hist 是本轮有明确收益的路径：前端节省 4.765 ms，整体周期节省 3.116 ms，
吞吐提升 6.44%。直方图＋CDF 细分由 7.116 降至 2.278 ms，其中双目硬件
作业约 2.201 ms；点数、LK 迭代数、提交数及 QR7 调用统计均保持一致。
平均吞吐超过 20 FPS，但 P95 仍超过 50 ms，不能称为每帧稳定 20 Hz。

首轮 ST 替换不能作为等工作量加速结论：FAST threshold=50，而 ST 质量阈值
设为 3／255，两者评分和阈值语义不同。相同目标 `num_pts=150` 分为每路 75，
5×5 网格的 `cell_target=75/25+1=4`；这是补点目标，不是最终严格截断上限。
ST 更容易补满网格，实际点数约增加 34%。检测本身由 14.045 降至 11.928 ms，
但细化由 3.342 增至 6.559 ms，LK 由 14.456 增至 22.786 ms。
窗口内 LK 总点迭代由每帧 3,983.5 增至 7,445.7，说明除了更多点，还存在
更高的跟踪迭代工作量。全 1040 帧 QR7 核心调用由 11,878 增至 35,157。
这证明当前 ST 点集使后续处理更重，尚不能把它归因于硬件 ST 算子速度。
后续将 ST 阈值调至实际点数接近 FAST，再作速度对照，见下面的补充实测。

完整测量输出位于 `install/openvins-c906-frontend/aligned-images/results/`，
汇总为 `ive-comparison.json`。首轮按要求只测吞吐；实验退出后已
恢复原小核固件和 Linux TPU／IVE 驱动，正式应用未替换，三个新功能均默认关闭。

### ST 质量阈值调参：接近 FAST 的实际点数

保持上述 FP64 参数和同一 RAM 窗口，仅调整 `OPENVINS_TPU_IVE_QUALITY`。
FAST 基准的左／右均值为 49.8／54.5，双目合计 104.33 点；下表各 ST 组
同时启用 IVE Hist，未改变点数目标、网格或最小间距，也没有增加硬截断。

| ST quality | 左／右点数均值 | 双目总数相对 FAST | 前端有效时间 | 整体周期 | 吞吐 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 3 | 73.3／66.5 | +33.97% | 41.801 ms | 70.439 ms | 14.20 FPS |
| 12 | 54.8／59.4 | +9.40% | 34.575 ms | 52.857 ms | 18.92 FPS |
| **16** | **48.4／55.0** | **−0.88%** | **33.896 ms** | **46.327 ms** | **21.59 FPS** |
| 20 | 42.3／49.6 | −11.90% | 32.201 ms | 44.200 ms | 22.62 FPS |

选择 **16**，而不是点数明显偏少的 20。与相同 IVE Hist、仍用 FAST 的基准
相比，点数接近时加入 ST，前端 35.724→33.896 ms，整体周期
48.392→46.327 ms，吞吐 20.66→21.59 FPS，额外提升 4.46%。
与最初 C906＋TPU、IVE 全关闭的 19.41 FPS 相比，Hist＋ST 合计提升 11.18%。
quality=16 的周期 P95 为 67.074 ms，仍不能称为每帧稳定 20 Hz。

实验程序默认值和 `benchmark_ive.sh` 已统一为 16，可显式覆盖：

```sh
OPENVINS_TPU_IVE_QUALITY=16 OPENVINS_IVE_MODES=both \
  ./benchmark_ive.sh CONFIG DATASET quality-16 1040
```

这是该 640×480 FPV 窗口上的近似等点数比较；ST／FAST 的点位置、存活时间和
后续几何工作量仍不同，未据此推断轨迹精度。两种分辨率的布局支持保持不变。
原始输出分别在 `aligned-images/quality-12`、`quality-16`、`quality-20`，
汇总在 `install/openvins-c906-frontend/aligned-images/quality-tuning.json`。

### ST=16 完整序列精度对照

在同一台 TideSense A53=1 GHz 上回放完整 `indoor_forward_6`，共 1970 对
640×480 图像、约 69.60 秒数据。两组均使用 IVE Hist、相同 FP64 后端、
标定、噪声、网格和点数目标，仅将 FAST 换为 ST=16。没有使用回环或重定位。

```sh
# 设备实验目录，与上面的程序、固件和配置相同；0 表示完整回放。
OPENVINS_TPU_IVE_QUALITY=16 OPENVINS_IVE_MODES="hist both" \
  ./benchmark_ive.sh \
  /mnt/data/openvins-c906-front/tail-latency/config/speed/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset accuracy-st16 0

# 取回 accuracy-st16/ 后，沿用此前 FPV 的位置 ATE 评估协议。
python3 scripts/vio-compare/probes/qr7point/evaluate.py \
  install/openvins-c906-frontend/aligned-images/accuracy-st16 --modes hist both
```

两组均完成 1970 帧，均在第 189 帧（从 0 计，约 6.369 秒）初始化，
初始化后各输出 1781 帧有限位姿。该数据集真值只覆盖约 30.20 秒，评估使用
两组共同覆盖的 816 帧、30.154 秒，采用 SE(3) 刚体对齐，**不拟合尺度**。
沿用已有评估的固定时间戳修正 −16.684572 ms，没有分别搜索最优时间偏移。

| 共同真值区间的位置误差 | FAST＋IVE Hist | ST=16＋IVE Hist |
| --- | ---: | ---: |
| ATE RMSE | 0.286722 m | 0.305188 m |
| ATE P95 | 0.422960 m | 0.428235 m |
| ATE 最大值 | 0.438557 m | 0.457905 m |
| 最后一个有真值帧的误差 | 0.197884 m | 0.216259 m |

本序列上 ST 的位置 RMSE 增加约 **6.44%**，绝对增加约 **1.85 cm**，属于
小幅精度损失；共同真值区间未见明显位置发散。完整回放未出现非有限位姿或
初始化后返回未初始化状态，也没有出现双目 LK 有效点合计少于 10 的帧。
这些检查不能替代真值未覆盖片段的精度结论，也不是其他序列的精度保证。

原始轨迹、日志、共同真值对齐后的误差 CSV、轨迹／误差曲线及补充统计位于
`install/openvins-c906-frontend/aligned-images/accuracy-st16/`，分别为
`hist.csv`／`both.csv`、`accuracy.json`、`full-accuracy.json`、
`aligned-errors.csv` 和 `trajectory-st16.png`。实验退出后已恢复原小核固件和
Linux TPU／IVE 驱动。

### 拟采用的 C906 调度

* Linux 在进程启动时交出硬件 IVE，负责时钟、复位、共享内存及退出恢复；
  C906 运行期间由小核独占，避免 Linux 驱动重新配置同一组寄存器。
* 只移植 Hist 的最小寄存器配置、DMA 地址、启动和有超时的完成检查，复用
  现有物理灰度缓冲。对 DMA 输入执行必要的 clean，对计数输出执行 invalidate。
* C906 读取 256 桶计数后沿用原 CDF、常值图处理和舍入规则，TPU 继续应用 LUT。
  A53 每帧仍只发一次完整前端请求，不插入逐阶段往返。
* 可进一步尝试先完成左图 Hist，再启动右图 Hist，同时执行左图 TPU LUT／
  金字塔。这是 IVE 与 TPU 两个引擎的并行，尚未测量。
* 为新路径增加默认关闭的独立开关；检查固件代码、BSS 和栈的预留空间，
  当前固件空间紧张，不搬入整套 Linux IVE 驱动。

验证顺序：C906 可访问性与超时、双目全部桶／LUT／像素一致性、并发缓存与
所有权、完整 FPV 回放精度、相同 RAM 窗口吞吐。先关闭 CPU 尾部分流作等价
对照，再测组合模式。C906 控制不会让 IVE 硬件本身凭空变快，收益需实测。

### 其他可用算子与优先级

| 候选 | SDK 支持及用途 | 对当前前端的判断 |
| --- | --- | --- |
| ST 候选响应图，非完整 Shi–Tomasi 检测器 | 当前 SDK 的硬件 IVE 有 `STCandiCorner`：特征值计算及候选阈值处理，输出 U8 图；本轮已通过常值／直边／棋盘格基本检查 | 仍需软件提取坐标、排序、执行间距及网格选点，并核对硬件局部筛选与当前 NMS 规则；不能当作可直接替换 FAST 的完整检测器。点集、分数与阈值语义会变化，需要独立验证完整 FPV 精度及整条检测链耗时 |
| Scharr／梯度 | Sobel 接口有可配置的 5×5 有符号系数，理论上可嵌入 3×3 Scharr 核，并输出 H／V 的 S16 图 | 需要验证系数方向、边界和位宽；当前 TPU 已按窗口计算且梯度留在片上，生成整图梯度会增加 DDR 写读、重排及格式转换，未必更快 |
| 高斯／金字塔 | Filter 支持 5×5 系数和右移归一化，另有 Resize 及 DMA 间隔复制 | 可验证高斯滤波＋抽样；双线性 Resize 不是 `pyrDown` 的等价替代。现有双目 TPU 金字塔约 2 ms，先核对 REFLECT_101 边界、舍入和分块接缝，优先级低于 Hist |
| DMA／掩码处理 | DMA、And、Or、阈值、形态学可搬运或处理整图 | 仅在能合并整图遍历／拷贝时考虑；少量点的画掩码、网格计数、排序和 ID 维护仍适合 C906，多个小作业的配置开销可能超过节省 |
| SAD／NCC | 可用于块匹配或匹配质量检查 | 适合另做立体粗匹配／深度，或实验性质量门控；不能直接替代 LK 的亚像素采样、梯度残差、迭代求解和收敛判断 |

需要区分两套 API：本地 `ive/include/ive.h` 图像库未提供 `STCandiCorner`，
但 `cvi_mpi/include/cvi_ive.h` 有该接口，实际寄存器实现位于硬件驱动
`cvi_ive_STCandiCorner()`，内部调用 `_CVI_HW_STBoxFltAndEigCalc()` 与
`_CVI_HW_STCandiCorner()`。不能将这两套库的能力混为一谈。

针对板上硬件的补充冒烟测试使用直接 ioctl 并检查返回值，输出预先填入
0xA5，再 invalidate 后统计，确认结果被实际写回：

| 输入 | 64×64 输出非零像素 | 640×480 输出非零像素 |
| --- | ---: | ---: |
| 常值灰度 | 0 | 0 |
| 单条竖直边 | 0 | 0 |
| 8×8 格子的棋盘格 | 196 | 18644 |

这里统计的是响应图非零像素，**不是最终角点数**。640×480 单次 ioctl wall
约 2.49–2.72 ms，包含驱动完整处理，不含后续软件选点；仅为各图一次的基本
功能检查，未验证 OpenCV Shi–Tomasi 数值等价、自然图像质量或 FPV 精度，不能
据此认定能加速当前前端。源码与结果分别为上述实验目录的
`corner-probe.c`／`corner-results.csv`。

除 Hist／Map 的重复测量和 ST 候选图的基本检查外，其余候选尚未做本轮硬件
耗时或数值等价测试。尤其 640 像素
宽的 Filter／Sobel 在现有驱动中会进入宽度大于 480 的分块路径，C906 移植必须
处理该限制，不能仅搬一次寄存器设置。单个 IVE 引擎的 Hist、Map、Sobel 等
作业还要串行排程，不能把每项估计收益相加；与 TPU 的并发也需计入 DDR 争用。
这些方案作用于像素前端，RANSAC、特征库维护和 FP64 后端的长尾仍需分别处理。

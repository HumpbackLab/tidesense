# RV1103：ARM32 OpenVINS 后端优化

在已有 RKIVE 金字塔 + LK 版本上测试本地 ARM32 后端记录。内核、设备镜像和
原来的 FP64 可执行文件均未替换。测试程序部署到 `/root/backend-test-20261025`。

## 实测结果（2026-10-25）

以下为不启用 perf 的完整回放；每轮 400 个初始化后测量帧。

| 后端 | 前端均值 ms | 后端均值 ms | 纯 VIO 均值 / P95 ms | ATE cm |
| --- | ---: | ---: | ---: | ---: |
| FP64，Givens，原分块乘法 | 24.925 | 79.223 | 105.275 / 221.493 | 5.100 |
| FP64，仅 Householder | 24.932 | 68.622 | 94.682 / 175.208 | 5.100 |
| FP32，Givens，原分块乘法 | 25.682 | 56.242 | 83.044 / 178.596 | 5.109 |
| FP32，Givens，矩阵打包 | 24.489 | 50.965 | 76.540 / 155.738 | 5.117 |
| FP32，Householder，原分块乘法 | 24.375 | 35.749 | 61.236 / 101.650 | 5.108 |
| **FP32，Householder，矩阵打包** | **24.570** | **34.430** | **60.087 / 101.909** | **5.117** |

组合方案相对同日 RKIVE + FP64 基准，后端耗时减少 **56.5%**，纯 VIO 减少
**42.9%**。纯 VIO 平均吞吐约 **16.64 fps**；图像读取解码另加 22.45 ms 后，
平均 82.53 ms、约 **12.12 fps**，P95 为 128.15 ms，尚未达到 20 fps。
前端保持约 24.6 ms，当前后端仍占约 57%。

组合方案后端内部均值：必要三角化 2.981 ms、约束系统/外点检验 12.503 ms、
测量压缩 6.059 ms、EKF 8.565 ms，其余包括传播、清理和边缘化。
FP64 基准的测量压缩为 25.790 ms、EKF 为 22.837 ms。
单独 FP32 仍使用 double 系数的 Givens，测量压缩有 24.429 ms；这解释了
只改矩阵精度无法消除该热点。仅改 FP64 QR 可减少约 10.1% 纯 VIO 耗时。

全部模式退出 0，完整输出 405 个有限状态，协方差对角线保持正值，χ² 门控生效。
FP32 组合与 FP64 基准的未对齐位置差 RMS 为 1.53 cm、末位置差 5.71 mm、
末姿态差 0.219°；相同短序列 ATE 接近，并不代表逐帧等价。
仅改 FP64 QR 的位置差 RMS 约 0.23 µm、末位置差约 0.78 µm。
FP32 组合最小记录协方差对角项约 7.98e-9。没有做完整协方差特征值检查。

每个无 perf 模式目前一轮；矩阵精度和 QR 舍入会使门控/后续矩阵尺寸略有变化，
这些是同配置端到端测量，不是严格相同 Jacobian 的内核微基准。

最快组合另做完整 perf 复跑：纯 VIO 60.305 ms，后端 34.621 ms，轨迹 CSV 与
无 perf 轮次 SHA256 完全相同。2523 个样本中 2271 个落入测量帧的纯 VIO 区间；
没有 LOST/THROTTLE 记录。主热点已变为 FP32 Eigen `gebp_kernel`（23.21%）及
直方图均衡两部分（合计 8.71%），不再由原先 FP64 乘法与 Givens 压缩主导。
符号文件与板端程序的 Build ID/部署 SHA256 已核对。

本次共出现两次 RVE 驱动 `running job is invalid on irq thread` 告警，分别在
FP64 基准和 FP32 Householder 回放期间。SDK rve_drv.c 的触发条件是 IRQ 线程中
`running_job == NULL`；是否为多余中断或任务生命周期竞态尚未定位。
对应回放都完成、没有 API 查询失败，最快组合两轮未新增此告警，但不能据此
宣称硬件驱动完全无异常。未为此修改内核。结束后 governor 恢复 ondemand，
RVE pd_ref=0，DMA heap 分配回到 0，回放进程均已退出。

## 来源与实现

- `/home/ncer/fpga_vio/docs/OPENVINS_FP32_BACKEND_EXPERIMENT.md`：Cortex-A7 FP32
  后端记录。采用 `/home/ncer/fpga_openvins/third_party/open_vins/fp32` 已修复的
  materialized overlay，基于本次 FP64 回放的干净源码快照构建。
- `/home/ncer/fpga_openvins/benchmarks/cpu_boundary/OPENVINS_MONO_A7_RESULTS.md`：
  外点门控修复、11 clones 和原生初始化的单目复测。
- [backend-f32](../backend-f32/README.md)：`P Hᵀ` 协方差列打包，一次矩阵乘法
  替换按状态小块累加，使用运行时开关单独测试。
- `/home/ncer/fpga_vio/third_party/open_vins/ov_msckf/src/update/UpdaterHelper.cpp`：
  Householder QR 测量压缩实现。只移植这一函数的实现思路；保留原 Givens 路径，
  不移植参考工作树中删除 χ² 门控等无关改动。

FP32 覆盖 state/update 的协方差、Jacobian、残差、更新及求解矩阵；时间戳、
名义状态、相机接口和 Ceres 初始化仍为 FP64。原 Givens 路径的旋转系数仍为
double。传播和边缘化后按已有 overlay 恢复协方差对称性，不钳位负对角项。
FP64 和 FP32 使用各自一致的头文件/对象，未混用 State ABI。

Householder 使用 `Qᵀ residual` 和上三角 R，不显式构造完整 Q，不改残差权重、
状态维度和行截断规则；其浮点舍入路径仍与 Givens 不同。另测仅替换 QR 的
FP64 版本，以区别算法实现和精度改变的收益。

## 测试条件

RV1103 单核 Cortex-A7，固定 performance/1104 MHz，静态 GCC 11 ARMv7 hard-float，
`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -O3`，Eigen 3.4。
反汇编的 FP32 UpdaterHelper 对象中有 `vmul.f32 q*` 等 NEON 向量指令。

EuRoC V1_01 25 秒片段，752×480 PGM，单目、200 目标特征、11 clones、max_slam=0，
FEJ 和在线标定开启，保留 χ²/RANSAC。原生初始化，5 帧预热 + 400 测量帧；
每轮共处理 495 帧，输出 405 个初始化状态。所有模式采用同一 RKIVE 前端配置。
ATE 按完整初始化轨迹做无尺度 SE(3) 对齐，真值不参与初始化。
图像读取解码单独计时；纯 VIO 不包含这部分。

## 复现

在 SDK 根目录执行，使用本次已保存的 FP64 回放源码及静态依赖：

```bash
probe=scripts/vio-compare/probes/rv1103-profile
work=$PWD/install/rv1103-backend-20261025
python3 "$probe/prepare-backend.py" install/rv1103-openvins-20261025 "$work"
BUILD_DIR="$work/build" BUILD_JOBS=3 \
  bash "$work/src/benchmarks/openvins_replay/build.sh" a7 -DOPENVINS_FP32_BACKEND=ON
python3 "$probe/build-rkive.py" --base "$work" --output "$work/rkive"
python3 "$probe/build-rkive.py" --output "$work/fp64-householder" \
  --replace-source "$work/src/third_party/open_vins/ov_msckf/src/update/UpdaterHelper.cpp"
```

prepare 脚本拒绝覆盖已有源码；复现到新目录时更换 work。板端部署两个静态程序
为 `bin/openvins_mono_fp32`、`bin/openvins_mono_fp64`，dataset/share 链接到已有
`/root/openvins-profile-20261025`。复制本目录的 run-backend-board.sh 和
run-rkive-board.sh 后：

```sh
sh /root/backend-test-20261025/run-backend-board.sh fp32
sh /root/backend-test-20261025/run-backend-board.sh fp32-packed
sh /root/backend-test-20261025/run-backend-board.sh fp32-householder
sh /root/backend-test-20261025/run-backend-board.sh fp32-packed-householder
sh /root/backend-test-20261025/run-backend-board.sh fp64-householder
```

脚本选择对应精度的程序，设置独立开关，计时期间切 performance，退出时恢复。
设置 `OPENVINS_RUN_MODE=perf` 可采样相同完整回放。取回 results 后：

```bash
python3 scripts/vio-compare/probes/rv1103-profile/analyze-backend.py \
  install/rv1103-backend-20261025
```

原始 CSV、日志、perf、源码清单与编译命令在
`install/rv1103-backend-20261025/`；完整统计为该目录的 backend-results.json。
协方差有限性、非负对角线和短序列 ATE 通过，不等于长期数值稳定或严格半正定证明。

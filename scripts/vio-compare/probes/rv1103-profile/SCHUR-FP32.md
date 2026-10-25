# RV1103：ov-SchurVINS FP32 后端与矩阵打包

在上次 [ov-SchurVINS 单目回放](SCHUR.md) 的独立源码快照上，移植 OpenVINS 已验证过的
FP32 生成方法及协方差列打包。工作目录为 `install/rv1103-ov-schur-fp32-20261025`，
板端部署于 `/root/ov-schur-fp32-test-20261025`。

## 实测结论（2026-10-25）

**完整 FP32 实验版能编译，但未通过完整回放；不能照搬 OpenVINS 的全后端转换。**
保留 FP64 协方差、传播及滤波更新的混合版完成了回放，收益有限且轨迹误差增加。
因此脚本默认选择 FP64；FP32 和混合精度均为显式实验选项。

全部成功轮次为同一 RKIVE 前端、495 张图像、5 预热 + 400 测量帧、405 个初始化状态。
下表纯 VIO 不含图像读取解码及数值诊断，ATE 使用无尺度 SE(3) 对齐：

| 模式 | 后端均值 ms | 纯 VIO 均值 ms | ATE cm |
| --- | ---: | ---: | ---: |
| FP64 原始乘法 | 28.849 | 54.567 | 35.205 |
| FP64 打包 | 27.706 | 53.430 | 35.205 |
| FP64 Joseph 对照 | 43.594 | 69.314 | 35.205 |
| FP32 Schur / FP64 状态与滤波器 | 28.175 | 53.548 | 39.131 |
| FP32 Schur / FP64 状态与滤波器，加打包 | 26.876 | 52.317 | 42.862 |

混合 + 打包相对 FP64，后端耗时减少 **6.8%**、纯 VIO 减少 **4.1%**，ATE 增加
约 7.7 cm。含图像读取均值 **74.393 ms，约 13.44 fps**。P95 纯 VIO 为 66.04 ms，
不能据此声称每帧 50 ms。该片段上的收益和误差不支持替换现有 FP64 默认。

细分均值（ms）体现收益所在：

| 阶段 | FP64 原始 | 混合 + 打包 |
| --- | ---: | ---: |
| 必要三角化 | 2.909 | 2.895 |
| Jacobian / 正规方程构造等 | 2.067 | 1.899 |
| Schur 补消元 | 2.588 | 2.056 |
| SV_EKFUpdate 及噪声构造 | 17.308 | 16.065 |

FP64 原始乘法轨迹与上一轮 Schur + RKIVE 完全一致。FP64 Joseph 对照也完成，
ATE 与原始相同，但增加约 14.7 ms 纯 VIO 耗时。混合模式的三角化接受分支和更新数
会随数值路径变化；成功退出不能当作精度等价。

## 完整 FP32 的数值失败

| 尝试 | 失败前阶段记录数 | 结果 |
| --- | ---: | --- |
| 原始相减式协方差更新 | 0 | 首次 SV_EKFUpdate 触发负对角检查 |
| 打包 + 原始协方差更新 | 0 | 同样退出 |
| Joseph 协方差形式 | 188 | 未完成 400 帧，退出 1 |
| 打包 + Joseph | 26 | 未完成 400 帧，退出 1 |
| 打包 + Joseph，创新矩阵/求解临时使用 FP64 | 26 | 逐帧检查发现非有限状态或协方差 |

阶段记录包含预热，失败帧的阶段和轨迹写入边界不同，不能直接把该数值当作有效
性能样本数。失败轮次均未生成有效完整 benchmark JSON，不报告其平均吞吐。

首个失败在 QEMU 软件前端下也复现，GDB 栈为 SV_EKFUpdate → UpdaterSchurVINS →
VioManager。抓取的首个更新输入中，创新矩阵条件数约为 1e5 量级；相减式更新出现
约 -1.3e-3 的对角项。对同组输入改用 Joseph 形式可避免该次负对角，但完整 FP32
仍在后续失败。仅提高创新矩阵计算精度的抽检中，记录到约 -6.1e-7 的协方差最小
特征值，随后出现非有限值。说明只修补第一次相减并不足以保证这条路径的数值稳定。
矩阵转储文本精度有限，条件数用于判断量级；没有用对角/特征值钳位绕过检查。

失败文件、调用栈和矩阵转储分别保存在 FP32 工作目录的 failed-direct、failed-joseph、
exit-backtrace.txt、first-update 中。混合版另存于 `install/rv1103-ov-schur-mixed-20261025`。

## 可运行的混合版

`prepare-schur-mixed.py` 从 FP32 实验源码生成新的、完整重编译的混合构建：
State、Propagator、StateHelper 及协方差保持 FP64；update 目录的 Jacobian、残差、
W/V、正规方程和 Schur 消元使用 FP32；送入 SV_EKFUpdate 时显式转换到 FP64。
存在可变矩阵输出的 SLAM initialize 接口增加显式转换适配，不混用 State ABI。
本次仅验证 mono/max_slam=0，未把适配器编译通过当作 SLAM 功能测试通过。

混合版保留原始相减协方差形式，打包在 FP64 滤波器内执行。它不是完整 FP32 后端，
JSON 明确标记 `fp32-schur-fp64-state`。

混合 + 打包另做独立数值诊断和 perf 复跑，三轮轨迹 CSV 逐字节一致。
数值诊断覆盖 405 个状态，最小协方差对角项约 3.41e-9；10 次最小特征值抽样的
最低值约 -3.10e-14，全部原样记录，因此也不宣称协方差严格半正定。
数值诊断轮次 VIO 均值 52.443 ms，perf 轮次 52.993 ms。
perf 共 2239 个样本，1992 个落在测量帧纯 VIO 区间，没有 LOST/THROTTLE。
FP64 Eigen gebp_kernel 仍占 27.46%，直方图均衡合计 10.09%，说明主要矩阵热点
仍位于保留的 FP64 滤波更新。符号 Build ID 和板端 SHA256 已核对。

本轮没有新增 RVE IRQ 告警。结束时无回放进程，governor 恢复 ondemand，
RVE pd_ref=0，DMA heap 分配回到 0。所有结果仅适用于当前 25 秒片段及配置。

## 转换范围

基于本地 `probes/backend-f32/prepare.py` 的转换规则，为 ov-SchurVINS 的 state/update
目录生成独立 fp32 overlay。增加 Schur 专用接口的转换，覆盖：

- 状态协方差和传播矩阵；
- 特征 Jacobian、残差、W、V/V_inv、梯度、Schur 正规方程及消元；
- SV_EKFUpdate 的创新矩阵、求解、Kalman 增益及协方差更新。

时间戳、名义状态 ov_type、相机模型及 Ceres 初始化接口继续使用 FP64。
相机 Jacobian 的输出参数、FeatureInitializer::ClonePose 增加显式 double/float 边界转换。
Eigen 混合类型规则与已有 OpenVINS FP32 方案一致；这仍是较宽的混合精度实验。
Huber 权重及部分标量运算仍为 double，不能描述为所有后端指令均改成 float。

传播和边缘化后沿用 OpenVINS FP32 的协方差显式对称化。未钳位负值，未关闭原有
负对角项检查。Huber 参数、噪声、滑窗、特征数量、更新公式均保持不变。
FEJ/在线标定仍按该分支要求关闭，必要三角化保留，活跃点可视化重三角化关闭。

Schur 更新不经过 MSCKF 的 measurement_compress，因此不套用 Householder QR。
本次复用的优化为 FP32 和 P Hᵀ 打包：先按 H_order 收集对应协方差列，再执行一次
`P_meas * H.transpose()`，替换按状态/观测小块的多次乘法累加。
运行时 `OPENVINS_SCHUR_PACKED_MA=0|1` 切换；FP64 和 FP32 均可独立对照。

## ABI 与数值检查

CMake 选择原始 state/update 或完整 FP32 overlay，并向库和回放入口同时传播
Eigen 类型头及 include 顺序，避免混用 FP64 State 和 FP32 对象。
符号检查确认 FP32 `StateHelper::SV_EKFUpdate` 的参数实际为 Matrix<float>。
反汇编中有 `vmul.f32 q*`、`vadd.f32 q*` 等 ARM32 NEON 向量指令。

回放入口逐帧检查完整协方差、姿态、位置和速度的有限性；该检查位于 VIO 调用计时之外。
可选 `OPENVINS_SCHUR_NUMERIC=1` 额外输出逐帧对角线/非对称度，并在第 1、50、100、
…、400、405 个初始化状态，以 double 求协方差最小特征值，共 10 次抽样。
特征值检查同样位于计时之外，不截断负特征值。
性能轮次默认关闭该额外诊断；单独诊断轮次用于核对轨迹一致性与数值行为。

## 条件与复现

RV1103 单 Cortex-A7，performance/1104 MHz，静态 GCC 11 ARM32 hard-float、
NEON/VFPv4、Eigen 3.4，OpenCV/OMP 单线程。EuRoC V1_01 25 秒片段，752×480 PGM，
单目、200 目标特征、11 clones、max_slam=0，原生初始化，5 预热 + 400 测量帧。
所有模式使用同一 RKIVE 金字塔/LK 和运行时配置。

在 SDK 根目录使用新 work 路径构建（prepare 拒绝覆盖已有源码）：

```bash
probe=scripts/vio-compare/probes/rv1103-profile
work=$PWD/install/rv1103-ov-schur-fp32-20261025
python3 "$probe/prepare-schur-fp32.py" install/rv1103-ov-schur-20261025 "$work"
BUILD_DIR="$work/build" BUILD_JOBS=3 \
  bash "$work/src/benchmarks/openvins_replay/build.sh" a7 -DOPENVINS_FP32_BACKEND=ON
python3 "$probe/build-rkive.py" --base "$work" --output "$work/rkive-fp32"
python3 "$probe/build-rkive.py" --base install/rv1103-ov-schur-20261025 \
  --output "$work/rkive-fp64" \
  --replace-source "$work/src/third_party/open_vins/ov_msckf/src/state/StateHelper.cpp"
```

混合版构建：

```bash
mixed=$PWD/install/rv1103-ov-schur-mixed-20261025
python3 "$probe/prepare-schur-mixed.py" "$work" "$mixed"
BUILD_DIR="$mixed/build" BUILD_JOBS=3 \
  bash "$mixed/src/benchmarks/openvins_replay/build.sh" a7 -DOPENVINS_FP32_BACKEND=ON
python3 "$probe/build-rkive.py" --base "$mixed" --output "$mixed/rkive"
```

部署为 `bin/ov_schurvins_fp64`、`bin/ov_schurvins_fp32`，使用生成的 work/share。
在已有板端目录可直接执行：

```sh
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh fp64
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh fp64-packed
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh fp32
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh fp32-packed
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh mixed
sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh mixed-packed
OPENVINS_SCHUR_NUMERIC=1 sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh \
  mixed-packed mixed-packed-numeric-400 400
OPENVINS_RUN_MODE=perf sh /root/ov-schur-fp32-test-20261025/run-schur-fp32-board.sh \
  mixed-packed mixed-packed-perf-400 400
```

上面 fp32/fp32-packed 用于复现已知失败；可运行混合版部署为 `bin/ov_schurvins_mixed`。

取回 results 后，复用 `analyze-schur.py` 输出阶段统计、无尺度 SE(3) ATE 和数值诊断：

```bash
python3 scripts/vio-compare/probes/rv1103-profile/analyze-schur.py \
  install/rv1103-ov-schur-fp32-20261025
python3 scripts/vio-compare/probes/rv1103-profile/analyze-schur.py \
  install/rv1103-ov-schur-mixed-20261025
```

原始源码 SHA256、编译/链接命令、逐帧 CSV、协方差诊断、perf 与 JSON 保存在 work 下。

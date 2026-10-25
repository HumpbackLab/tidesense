# RV1103 ov-SchurVINS 单目回放

测试本地 `ov_SchurVINS`，并复用已验证的 RV1103 RKIVE 金字塔/LK 适配器。
板端程序与结果独立保存在 `/root/ov-schur-test-20261025`，工作目录为
`install/rv1103-ov-schur-20261025`。

## 实测结果（2026-10-25）

以下均为无 perf、原生初始化、400 个测量帧；每轮处理 495 帧并输出 405 个状态。
OpenVINS 对照和 Schur 均关闭 FEJ 与在线标定。

| 方案 | 前端均值 ms | 后端均值 ms | 纯 VIO 均值 / P95 ms | ATE cm |
| --- | ---: | ---: | ---: | ---: |
| ov-SchurVINS FP64，软件前端 | 67.488 | 28.694 | 97.230 / 118.987 | 18.119 |
| ov-SchurVINS FP64，软件四层/20 次对照 | 61.868 | 28.824 | 91.756 / 112.069 | 24.664 |
| **ov-SchurVINS FP64，RKIVE 前端** | **24.906** | **28.578** | **54.521 / 69.547** | **35.205** |
| OpenVINS FP64，RKIVE 前端 | 23.981 | 52.743 | 77.780 / 150.096 | 4.531 |
| OpenVINS FP32 + Householder + 打包，RKIVE 前端 | 24.349 | 26.254 | 51.673 / 79.868 | 4.531 |

Schur + RKIVE 相对 Schur 软件前端减少约 43.9% 纯 VIO 耗时。
相对同配置 OpenVINS FP64，Schur 后端耗时减少 45.8%、纯 VIO 耗时减少 29.9%；
相对已优化 OpenVINS，纯 VIO 均值则约慢 5.5%，但本次 P95 更低。
Schur 的较低延迟波动不能抵消这段数据上明显更大的轨迹误差。

Schur + RKIVE 图像读取解码另加 22.126 ms，完整均值 **76.647 ms，约 13.05 fps**；
纯 VIO 均值约 18.34 fps，P95 为 69.55 ms，尚未满足每帧 50 ms。
相同条件优化 OpenVINS 含读取均值 73.881 ms，约 13.54 fps。

Schur + RKIVE 细分均值 / P95（ms）：

| 阶段 | 均值 | P95 |
| --- | ---: | ---: |
| 特征测量清理 | 0.083 | 0.134 |
| 必要三角化和优化 | 2.901 | 5.926 |
| Jacobian / 正规方程构造等 | 2.042 | 4.805 |
| W 堆叠及 Schur 补消元 | 2.603 | 6.394 |
| 删除标记 | 0.007 | 0.010 |
| SV_EKFUpdate 及噪声构造 | 17.035 | 19.216 |
| Schur 更新合计 | 25.057 | 35.813 |

后端总计还包含 IMU 传播约 2.620 ms、清理/边缘化约 0.901 ms。
主要剩余开销在滤波更新，而非三维点 Schur 消元本身。

精度诊断：软件四层/20 次迭代对照的 ATE 为 **24.664 cm**，介于原软件的
18.119 cm 与硬件的 35.205 cm 之间。减少层数/迭代数本身就改变了误差；其余
硬件差异仍包括窗口、定点坐标及收敛阈值等，尚未逐项定位。不能把全部退化只归因于
层数，也不能依据这一段判断长序列性能。Schur + RKIVE 的估计路程为 9.593 m，
真值为 6.606 m；OpenVINS 对照估计路程约 6.942 m。ATE 没有通过尺度拟合掩盖误差。

软件版 405 次 Schur 调用、376 次完成更新；RKIVE 版为 405/379。
两者均有实际视觉更新，有限状态和协方差对角检查通过，RKIVE 版最小记录对角项
约 3.41e-9。这仅说明本次未触发这些数值检查，不代表误差合格或协方差严格半正定。
软件版轨迹与本地旧 A7 `clean_off.csv` 逐字节一致，SHA256 为
`1e638068e3b22e0c843932eae28ca1ac8e5f82bbb74ac336801040ca52e37b2f`。

perf 另跑完整 400 帧，VIO 均值 55.132 ms，后端 28.872 ms，轨迹与无 perf 版
逐字节一致。2330 个样本中 2070 个落入测量帧纯 VIO 区间，没有 LOST/THROTTLE。
FP64 Eigen `gebp_kernel` 占 28.24%，直方图均衡两部分合计 9.27%；
Schur 更新函数自身占 3.79%（不含其被调矩阵函数的样本）。符号文件与部署程序
的 Build ID/SHA256 已核对。

本轮两个新增 RVE `running job is invalid on irq thread` 告警，分别出现在
OpenVINS FP64 对照和 Schur + RKIVE 的无 perf 回放中；各轮均完成，API 未报错。
Schur perf 轮次没有新增该告警，仍复现相同轨迹。沿用原 SDK 驱动，IRQ 告警机制
尚未修复，详见 [后端实验记录](BACKEND.md)。所有回放结束后 governor 恢复
ondemand，RVE pd_ref=0，DMA heap 分配回到 0，无遗留回放进程。

## 源码与算法限制

源码复制自 `/home/ncer/fpga_openvins/third_party/ov_SchurVINS`，上游为
Jas0nG/ov_SchurVINS `feat/schurVINS` 分支，提交
`d988139c0ba3cc51abacb8b5b5acb482a19325ba`，GPL-3.0。
回放入口复制自同工程 `benchmarks/ov_schurvins_replay`，依赖继续使用本地
`/home/ncer/fpga_vio/.tools/a7-static`，没有重复安装。

此分支实现 Schur 状态更新，尚未实现原始 SchurVINS 完整的 EKF 地图点求解器。
FEJ 开启会让 Schur 更新提前返回，在线标定也不受支持，特征表示要求 GLOBAL_3D。
因此本次显式关闭 FEJ、相机内外参和时间偏移在线标定，并保留 FP64 后端。
原有 Huber 鲁棒权重和 Schur 更新公式不变。它没有沿用 OpenVINS/MSCKF 的逐特征
χ² 拒绝分支，不能把两种方法描述为完全相同的外点处理。

保留已有调用/完成更新计数。只新增前端和 Schur 内部计时，以及 RKIVE 调用包装；
不修改三角化、观测权重、噪声、滑窗、更新方程或数值失败检查。
关闭的仍是供可视化使用的更新后活跃点重三角化，估计所需三角化保留。

## 对照条件

RV1103，单 Cortex-A7，performance/1104 MHz，无 swap，OpenCV/OMP 单线程。
GCC 11 ARM32 hard-float 静态链接，NEON/VFPv4、Eigen 3.4、OpenCV 4.5.4。
EuRoC V1_01 25 秒片段，752×480 无损 PGM，200 目标特征，11 clones，max_slam=0。
原生初始化，5 帧预热、400 个初始化后测量帧；图像读取解码单独计时。
ATE 使用全部 405 个初始化状态、SE(3) 刚体对齐，不拟合尺度。

同时复制 OpenVINS FP64 和上一轮 FP32 + Householder + 打包的 RKIVE 程序，使用
相同的 FEJ/在线标定关闭配置运行，避免把减少标定状态带来的收益算作 Schur 收益。
两种方法本身的估计模型和鲁棒权重仍有差异。上一轮开启 FEJ/在线标定的 60.09 ms
属于不同配置；本次应优先与这里重新测得的 OpenVINS 对照比较。

RKIVE 配置与限制沿用 [RKIVE.md](RKIVE.md)：硬件四层/20 次迭代、S25Q7 坐标等，
与软件 LK 的数值路径不同。软件与硬件模式均保留全部特征，不做静默软件回退。

## 计时语义

Schur 的细分 CSV 包含 `schur_clean_ms`、`schur_triangulate_ms`、
`schur_system_ms`（观测 Jacobian、正规方程构造等）、`schur_eliminate_ms`
（W 堆叠、三维点消元和 Schur 补）、`schur_cleanup_ms`（标记特征删除）、
`schur_ekf_ms`（噪声构造及 SV_EKFUpdate）。没有把 Schur 的清理阶段标成 QR 压缩。
细分和粗分属于嵌套计时，不应相互重复相加。

## 复现

在 SDK 根目录构建；prepare 拒绝覆盖已有源码，复测构建时可换 work：

```bash
probe=scripts/vio-compare/probes/rv1103-profile
work=$PWD/install/rv1103-ov-schur-20261025
python3 "$probe/prepare-schur.py" "$work"
BUILD_DIR="$work/build" BUILD_JOBS=3 \
  bash "$work/src/benchmarks/openvins_replay/build.sh" a7
python3 "$probe/build-rkive.py" --base "$work" --output "$work/rkive"
```

prepare 同时生成 `$work/share`，其中 YAML 已为对照关闭 FEJ/在线标定；部署时使用
该目录，而非构建安装目录里保留上游默认值的 share。

板端已部署软件和 RKIVE 两个静态程序，以及相同配置的 OpenVINS 对照：

```sh
sh /root/ov-schur-test-20261025/run-schur-board.sh schur-software
sh /root/ov-schur-test-20261025/run-schur-board.sh schur-rkive
sh /root/ov-schur-test-20261025/run-schur-board.sh schur-software4
sh /root/ov-schur-test-20261025/run-schur-board.sh openvins-fp64-matched
sh /root/ov-schur-test-20261025/run-schur-board.sh openvins-optimized-matched
OPENVINS_RUN_MODE=perf sh /root/ov-schur-test-20261025/run-schur-board.sh \
  schur-rkive schur-rkive-perf-400 400
```

run-schur-board.sh 设置后端开关并选择程序；run-rkive-board.sh 固定频率并在退出时
恢复 governor。所有模式的运行时 YAML 关闭 FEJ 和在线标定。取回结果后：

```bash
python3 scripts/vio-compare/probes/rv1103-profile/analyze-schur.py \
  install/rv1103-ov-schur-20261025
```

完整原始 CSV、编译日志、源码 SHA256、perf 与统计 JSON 都保存在工作目录。

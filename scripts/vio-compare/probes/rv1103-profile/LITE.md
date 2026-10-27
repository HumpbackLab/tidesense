# RV1103 OpenVINS：VIO 本体的 20 Hz 计算预算

本次最终改动范围为 TrackKLT、FP32 UpdaterHelper、StateHelper 和运行参数。
最终程序使用原来的回放入口和图像加载器；所有下述 50 ms 预算均只统计 VIO 本体，
包含 IMU 入队、输入消息/掩码构造和 feed_measurement_camera，不将图片文件读取算入。

## 结果（2026-10-25）

RV1103 单 Cortex-A7，performance/1104 MHz，静态 ARM32 NEON/VFPv4、GCC 11、
Eigen 3.4、OpenCV 4.5.4，OpenCV/OMP 单线程，RKIVE 金字塔及 LK。
EuRoC V1_01 25 秒片段，**752×480 原分辨率**，mono/max_slam=0，原生初始化。
每轮 495 张图像，5 预热 + 400 测量帧，405 个初始化状态，覆盖同一段 20.2 秒轨迹。
ATE 为全初始化轨迹的 SE(3) 刚体对齐误差，不拟合尺度。

| 方案 | VIO 均值 / P95 ms | 超过 50 ms | ATE cm |
| --- | ---: | ---: | ---: |
| 前轮 FP32 + 压缩 Householder + 打包，200 特征/HIST | 60.087 / 101.909 | — | 5.117 |
| 本轮同参数基准 | 59.005 / 98.005 | 261 / 400 | 5.117 |
| 仅 NONE + 150 特征 | 41.970 / 67.431 | 116 / 400 | 5.004 |
| 仅 NONE + 120 特征 | 36.187 / 58.381 | 72 / 400 | 5.224 |
| 新实现，150 特征、单次最多 10 条 MSCKF 约束 | 38.771 / 50.465 | 25 / 400 | 4.828 |
| 新实现，120 特征、单次最多 10 条、精炼最多 2 轮 | 33.728 / 46.327 | 2 / 400 | 5.251 |
| **推荐：120 特征、单次最多 8 条、精炼最多 2 轮** | **34.055 / 44.214** | **3 / 400** | **4.972** |
| 推荐配置复跑 | 33.916 / 43.722 | 4 / 400 | 4.972 |

推荐两轮无 perf 合并 800 个测量帧：**均值 33.986 ms、P95 44.208 ms**；
7 帧超过 50 ms，最大 72.370 ms。对应约 99.1% 的测量帧在 50 ms 内完成。
按实测 VIO 耗时、20 Hz 到达、不丢帧模拟单线程队列，两轮末尾均无积压，
峰值完成延迟分别约 72.4/64.3 ms。这是计算预算/队列仿真，未作为实时摄像头全链路
20 Hz 实测；也没有证明每帧满足 50 ms 硬截止。

推荐配置保留 **11 clones、FEJ、相机内外参/时间偏移在线标定、RANSAC 几何阈值及
MSCKF χ² 门控**。未改成隔帧更新；全部输入图像按原时间戳处理。
本片段的 ATE 接近原配置不代表全场景精度等价，强运动和光照变化仍需另行验证。

## 前端改动

1. 关闭 HIST 均衡，目标特征由 200 改为 120；FAST 阈值、网格等沿用原值。
2. 每个相机批量调用 undistortPoints，替换每个点单独创建 Mat/调用函数。
   150 特征的完整校验轮次对每个输出点做逐位比较，全部通过；完整轨迹 CSV 与
   原逐点版本逐字节一致。Radtan 已实测，Equi 分支沿用同 API，未在鱼眼序列验证。
3. 在 RANSAC 前筛除 LK 状态失败/非有限的点，并将内点掩码映射回原索引。
   有效点不足 10 时保留原有保守的全拒绝策略。
4. RANSAC 最大迭代数设为 100，仍请求 confidence=0.999，仍用 2/focal 的归一化阈值。
   限制迭代后，高外点比例下不能保证达到所请求的统计置信度，需要接受这一取舍。

只减特征/关闭均衡的候选出现过约 150–170 ms 的 RANSAC 尖峰。
150 特征测试中，新迭代上限将其最大 RANSAC 耗时从 160.690 降到 16.168 ms。
所以只看 42 ms 的平均值会漏掉前端长尾。

## 后端改动

1. max_msckf_in_update 从 40 限为 8。沿用上游按轨迹长度选择的策略，减少参与当次
   更新的特征集合，属于信息量/计算量取舍，并非等价加速。
2. 特征三角化后的 LM 精炼最大轮数由 5 限为 2。必要线性三角化及质量检查保留。
3. 零空间投影沿用 Givens 形式，将旋转系数改为 float，与 H_f/H_x/res 的类型一致。
   新的 Householder 零空间候选在 150 特征配置下未完成回放，未进入推荐配置。
   全局测量压缩仍沿用上轮已验证的 Householder。
4. Kalman 增益由显式构造 S^{-1} 后乘 M，改为用同一 LLT 分解直接求解
   S Kᵀ=Mᵀ。数学模型、噪声及协方差更新式保留，FP32 舍入路径会改变。
5. 保留已验证的 FP32 协方差列打包。

推荐首轮细分（均值 / P95，ms）：

| 阶段 | 均值 | P95 |
| --- | ---: | ---: |
| RKIVE 金字塔 | 3.292 | 3.350 |
| 检测/补点 | 4.092 | 7.114 |
| RKIVE LK | 1.982 | 2.303 |
| 归一化 | 0.307 | 0.354 |
| RANSAC | 0.760 | 1.126 |
| 前端总计 | 12.293 | 15.337 |
| 约束构造、零空间和 χ² 检验 | 5.892 | 9.924 |
| 测量压缩 | 2.379 | 4.904 |
| EKF 更新 | 7.210 | 9.500 |
| 后端总计 | 20.713 | 29.381 |

细分属于嵌套计时，不能与总计重复相加，各阶段 P95 不能相加。

## 验证及未采用的配置

推荐首轮、复跑和 perf 轮次轨迹 CSV 逐字节一致，SHA256：
`ff53f52df250136cda39e96762c0a38cd6383849b173d29dc09fdd60ecd40521`。
全部 405 个记录状态有限，协方差最小对角项约 1.00e-8，未绕过负对角项检查。
没有将这些检查解释为完整协方差严格半正定或长期稳定性证明。

perf 轮次 VIO 均值 34.011 ms、P95 43.965 ms；1448 个 samples 中 1253 个落在
测量帧 VIO 区间，没有 LOST/THROTTLE。FP32 Eigen gebp_kernel 占 25.32%，
memcpy 7.29%，FAST 3.45%，get_marginal_covariance 3.10%。
符号文件 Build ID、部署 SHA256 已核对。

半分辨率候选 ATE 约 26.8 cm，未采用；关闭内外参在线标定的候选未完成回放，
未用失败片段的耗时作成绩。仅减至 100 特征的候选均值约 30.8 ms、P95 仍为
58.5 ms，说明减少点数不能替代对更新量和 RANSAC 长尾的控制。

已有 RVE 偶发 IRQ 提示仍可在中间试验日志中看到，见 RKIVE.md 中的 SDK 驱动限制。
最终退出后 governor 恢复 ondemand，RVE pd_ref=0，DMA heap 分配为 0，无遗留回放进程。

## 复现

工作目录：`install/rv1103-openvins-lite-20261025`；板端目录：
`/root/openvins-lite-20261025`。最终可执行文件是该目录 bin/openvins_mono。

```sh
sh /root/openvins-lite-20261025/run-lite-board.sh recommended
```

recommended 选择 n120-cap8-refine2 配置，并设置批归一化、有效点 RANSAC/100 轮、
float-givens、direct gain solve、FP32 打包及全局 Householder 压缩。
运行时环境变量可以显式覆盖，实际取值保存在各轮 variant.txt 中。

本机重新构建时选择新的 work 路径（准备脚本拒绝覆盖已有源码）：

```bash
probe=scripts/vio-compare/probes/rv1103-profile
base=$PWD/install/rv1103-backend-20261025
work=$PWD/install/rv1103-openvins-lite-20261025
python3 "$probe/prepare-lite.py" "$work"
python3 "$probe/prepare-lite-algorithm.py" "$work"
algorithm=$work/algorithm/open_vins
python3 "$probe/build-rkive.py" --base "$base" --output "$work/rkive-direct" \
  --track-source "$algorithm/ov_core/src/track/TrackKLT.cpp" \
  --replace-source "$algorithm/fp32/update/UpdaterHelper.cpp" \
  --replace-source "$algorithm/fp32/state/StateHelper.cpp"
python3 "$probe/analyze-lite.py" "$work"
```

原始逐帧计时、配置、轨迹、退出状态、perf 和构建命令在工作目录内。
20 Hz 队列分析明确只使用 VIO 本体耗时，未将文件输入耗时当作本次优化目标。

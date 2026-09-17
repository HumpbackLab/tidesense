# 四项优化全开后的热点

日期：2026-09-17。使用刚完成 align2D 优化的正式静态包同一可执行文件，SHA-256：
`7990f7b031a21b302529bbfa2bf6c7211f7277b69ca433533c3f0208d3b2b09f`。
开启 HALFSAMPLE、ZMSSD、FAST10、ALIGN2D 四个独立宏。本次只重新采样，没有继续修改算法。

## 当前排名

实板单 A53 / 800 MHz，GCC 11.3.1；同一 EuRoC V1_01 501 帧，去掉前五帧。下表为 VIO 区间内用户态 cycles 的 period 加权 **self** 占比，不包含子函数：

| 函数 | 之前三项 ON | 当前四项 ON |
|---|---:|---:|
| `align2D` | 34.56% | **21.52%** |
| `warpAffine` | 17.05% | **20.56%** |
| FAST 检测 | 4.09% | 4.92% |
| `SchurVINS::Solve3` | 4.41% | 4.91% |
| 相机 `backProject3` | 2.72% | 3.26% |
| `halfSample` | 2.19% | 2.62% |
| Eigen GEMM 内核 | 2.73% | 2.59% |
| 相机 `project3` | — | 2.07% |
| FAST NMS | 1.40% | 1.72% |
| FAST 评分 | 1.52% | 1.68% |
| 4×4 float 逆矩阵 | — | 1.41% |
| `getWarpMatrixAffine` | — | 1.19% |
| `updateZMSSD` | 0.49% | 1.06% |

align2D 和 warpAffine 合计 **42.08%**。二者排名接近，单轮约五千个用户采样不足以断言 1 个百分点的差距具有稳定意义。尾部小热点的百分比也会有采样波动。

warpAffine 占比上升不能解读为它变慢：乘以进程 CPU 均值作粗略量级估算，前后约 10.0 / 10.2 ms；align2D 约 20.2 / 10.7 ms。这里周期份额乘 CPU 时间只是近似，既不是函数独立计时，也不是调用次数计数；正式 align2D 收益以 [无 perf 交替回放](../bench/ALIGN2D.md) 为准。

## 调用来源

当前 self 采样的直接父级：

- `warpAffine ← depth_filter_utils::updateSeed`：占全部用户周期 **15.76%**，即约 77% 的 warpAffine self。
- `warpAffine ← reprojector_utils::matchCandidate`：**4.80%**。
- `align2D ← Matcher::findLocalMatch`：**15.77%**，约 73% 的 align2D self。
- `align2D ← Matcher::findMatchDirect`：**5.66%**。

包含子调用的 **inclusive**：`DepthFilter::updateSeeds` **45.23%**，`SchurVINS::Backward` **12.55%**，`Solve3` **7.20%**。这些比例互相嵌套，不能与 self 相加。

主要剩余工作仍在深度种子更新和重投影的 patch 处理，而非仅在后端矩阵求解。

## 下一步优化顺序

1. **先做 warpAffine。** [原实现](../../../../SchurVINS/svo_direct/src/patch_warp.cpp) 每个像素计算仿射坐标、floor、边界检查、插值权重和四点取样；当前 matcher 常用的是带边界的 10×10 patch。可以评估每行不变量复用、同一 patch 的批量坐标计算、边界检查快路径，以及浮点 NEON。A53 没有通用向量 gather，离散取样仍需处理，不能预先承诺 2 倍加速。应像 align2D 一样用独立宏隔离，核对所有输出字节、边缘失败行为和完整位姿；特别避免递增坐标累计误差改变 floor/uint8 截断。若该函数能达到 2 倍，按当前占比估算，用户计算耗时下降空间约 **10.3%**，这只是条件估算。
2. **再拆解 align2D 内部成本。** 仍有 21.5% self，但插值已做 SIMD。下一步应区分梯度/Hessian 准备与逐次残差累加，再考虑常用 offset-only 模型的专门路径；不能仅凭函数占比继续改浮点求和顺序。单独 4×4 inverse self 约 1.4%，只优化求逆的整体空间有限。
3. **相机几何及 warp 矩阵准备。** backProject3 + project3 self 约 5.33%，getWarpMatrixAffine 自身约 1.19%。可以检查参考关键帧中固定像素/层级的反投影是否能复用；相对位姿、深度和搜索层级会变化，不能直接缓存最终 warp 矩阵。这是候选方案，尚未测量命中率与收益。
4. **后端及工作量调整放后面。** Backward inclusive 约 12.6%；种子更新调度的潜力更大，但跳过已收敛种子、减少匹配尝试或降低迭代数会改变算法行为，需要单独验证精度。本轮未做这些改变。

halfSample 只占约 2.6%，继续缩图或考虑 IVE 不是当前第一优先级。

## 测量可信度与产物

- 日志、位姿、计时和 perf 数据全部写 tmpfs，输入读图在 VIO 区间之外。
- `perf record -F 199 -e cycles --call-graph fp --clockid mono`；按 CLOCK_MONOTONIC 精确过滤 496 个 VIO 区间。
- 区间内 4,852 个用户样本、48 个内核样本；无 LOST 事件，用户栈包含 addImageBundle 的加权覆盖率 **98.48%**，未发现未解析的用户 self。
- 带 perf：VIO 平均墙钟 **50.073 ms**，进程 CPU **49.688 ms**。内核周期占 VIO 总采样周期约 **0.98%**，主瓶颈是用户态计算。
- 501 帧全部 tracking / imu_fused 成功；位姿 SHA-256 与所有之前验证版本相同：`83e18631b20c8ff3657cb3a4075c1670dfbb87594dfc351f07e6f77b5241e7d2`。
- 使用同次链接的未剥离 ELF 经 symfs 解码；decode.log 无警告。原始 perf.data、symbols、samples.txt 在 `install/schurvins-gcc11-static/profile/align-ram/`。
- 可留存结果见 [summary.json](results/align-ram/summary.json)、[采样统计](results/align-ram/event-stats.txt) 和同目录逐帧区间、环境、哈希。

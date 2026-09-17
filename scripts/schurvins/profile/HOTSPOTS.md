# 当前 SchurVINS A53 热点复测

本文是前三项优化阶段的结果；最新四项全开采样见 [align2D 优化后热点](HOTSPOTS-ALIGN2D.md)。

日期：2026-09-17。结论：**旧表不代表当前构建，但上一轮用墙钟收益否定 CPU 热点的推断也不成立。同步 SD 日志写入掩盖了计算优化的收益。**

## 1. 先纠正测量中的 I/O 干扰

`/proc/mounts` 显示数据分区为 `/dev/mmcblk0p3 /mnt/data ext4 rw,sync,relatime`。
原测试把 stdout/stderr 重定向到这个分区。非 ROS 构建的
[`logging.h`](../../../../SchurVINS/svo_common/include/svo/common/logging.h) 中，
`SVO_DEBUG_STREAM` 即使 Release 也直接使用 `std::cerr`；连续的流输出产生很多小写入。

运行中实际观察到 `write(fd=2, ..., 6/11/13/... bytes)`，线程等待栈为：

```text
__arm64_sys_write -> vfs_write -> ext4_file_write_iter
  -> ext4_sync_file -> jbd2_complete_transaction -> jbd2_log_wait_commit
```

证据保存在 [sync-wait-evidence.txt](results/sync-wait-evidence.txt)。
原方式的全开程序在 496 个稳态区间平均墙钟 **361.545 ms**，进程 CPU **67.005 ms**，
即约 **81.5%** 的区间墙钟不是该进程的 CPU 执行时间；结合等待栈，主要问题是同步日志写入。
这不是对所有非 CPU 时间都逐项归因的 off-CPU 追踪。

本次将**日志、位姿、计时和 perf 数据全部写入 `/tmp` 的 tmpfs**，不改挂载选项，不关闭日志，
不改 VIO 参数和算法。输入图像仍在原 SD 分区，读取位于 VIO 计时区间之外。
原来的 `bench/RESULTS.md` 中约 2% 的数值描述的是这套 I/O 受限的测试，不能作为纯计算收益结论。

## 2. 当前 CPU 热点

下面均为用户态 `cycles` 的 **period 加权 self 占比**，不包含被调函数。
旧表只是对照；旧表缺少对应 ELF、原始 perf 数据和完整配置，不能判定它当时采错了。
特别是旧 FAST-9 与当前 FAST-10 已不是同一个检测路径。

| 热点 | 旧 hotpoint.md | 当前全关基线 | 当前三项全开 |
|---|---:|---:|---:|
| FAST 检测（旧 FAST-9 / 新 FAST-10） | 23.40% | 10.45% | 4.09% |
| FAST 评分 | 9.32% | 2.87% | 1.52% |
| align2D | 9.69% | 29.87% | 34.56% |
| warpAffine | 7.23% | 15.52% | 17.05% |
| NMS | 5.19% | 1.34% | 1.40% |
| halfSample | 6.69% | 6.34% | 2.19% |
| updateZMSSD | 3.82% | 1.03% | 0.49% |

**当前全开后，align2D + warpAffine 自身合计约 51.6%，是下一阶段的首要计算热点。**
它们的占比上升主要因为另外三个内核变快、分母变小，不代表它们发生了同等比例的退化。
FAST 检测＋评分＋NMS 当前基线约 14.66%，并不是旧表的 37.91%。

当前其他主要 self：`SchurVINS::Solve3` 约 4.0%（基线）/4.4%（全开），
Eigen 通用矩阵乘内核约 2.0%/2.7%；不能据此把整个后端等同于这两个 self 项。
完整列表见 [base-ram/summary.json](results/base-ram/summary.json) 和
[all-ram/summary.json](results/all-ram/summary.json)。

## 3. 调用来源与执行频率

以下是**包含子调用的 inclusive 占比，不能与上面的 self 相加**：

| 调用路径 | 基线 inclusive | 全开 inclusive |
|---|---:|---:|
| `DepthFilter::updateSeeds` | 43.47% | 49.40% |
| `Matcher::findLocalMatch` | 23.33% | 26.53% |
| `SchurVINS::Backward` | 9.49% | 11.01% |
| `SchurVINS::Forward` | 2.54% | 3.37% |

全开版中，约 75% 的 align2D self 采样直接来自 `findLocalMatch`，
约 77% 的 warpAffine self 采样直接来自 `depth_filter_utils::updateSeed`。
因此主要关注点是**深度种子更新中的小 patch 变换和局部对齐**。

日志还提供了独立的工作量证据：501 帧合计 1,000 批种子更新，322,329 次成功更新；
同样去掉前 5 帧后有 992 批、322,143 次成功更新，约 **649 次/双目帧**。
这是成功更新计数，不是精确的 align2D 调用次数，实际匹配尝试还包含失败路径。
基线和全开的这些计数相同。

[`DepthFilter::updateSeeds`](../../../../SchurVINS/svo_direct/src/depth_filter.cpp) 会遍历多个参考关键帧的种子，
并向 `updateSeed` 传入 `check_convergence=false`，因此已收敛种子也可能继续更新。
源码注释明确这是精度选择，不能简单当作 bug 删除。
`max_n_seeds_per_frame=240` 也不等于每个输入帧总共只做 240 次更新。

## 4. 本次采样可信度与范围

- 实板单 A53、800 MHz；GCC 11.3.1，`-O3 -mcpu=cortex-a53`，静态链接、保留 frame pointer。
- 同一 EuRoC V1_01 片段：501 组 752×480 双目图像，排除前 5 帧，保留 496 个 VIO 区间。
- `perf record -F 199 -e cycles -g --call-graph fp --clockid mono`；以程序导出的 CLOCK_MONOTONIC 起止时间过滤，剔除读图、预热和主循环的其他工作。
- 用原始周期 period 加权；用户态/内核态分开统计。过滤后基线有 6,572 个用户样本、35 个内核样本；全开有 5,723 / 44 个。
- 无 LOST 事件；用户调用栈包含 `addImageBundle` 的加权覆盖率分别为 99.16% / 98.85%，未解析用户 self 约 0.01% / 0.02%。尾调用和内联仍会影响父子符号呈现。
- 上传的是剥离版，但本地保留了同次链接的未剥离 ELF，并核对 SHA-256。该工具链生成的应用没有 GNU build-id，不能只依赖 build-id 自动找符号。
- 内核 ELF 的 build-id 与采样记录一致：`e30a2bbe2c818d2631772bf501f69485ce66a3d8`。
- 两次采样均 501 帧跟踪及 IMU 融合成功，位姿 CSV 与之前的所有配置完全相同。

带 perf 的复测结果：

| 配置 | VIO 墙钟均值 | 进程 CPU 均值 |
|---|---:|---:|
| 基线 | 67.449 ms | 67.044 ms |
| 三项全开 | 59.162 ms | 58.472 ms |

CPU 耗时降低约 **12.8%**，墙钟降低约 **12.3%**；它们是带采样器的结果。
按当前各内核 self 占比和单测加速比粗略估算，预期 CPU 耗时下降约 13%，与这个结果相符。
无 perf 补测：基线 66.578 ms，全开 58.089 ms，耗时下降 **12.75%**；单独 halfSample / ZMSSD / FAST-10 分别下降 **3.96% / 0.56% / 8.36%**。各开关详细结果见 [bench/RESULTS.md](../bench/RESULTS.md)。

## 5. 下一阶段的优先级

1. **先处理运行时日志策略。** 保留必要错误信息，调试日志应可关闭或缓冲到内存，避免在热路径同步写 SD；仅修改算法并不能消除这种等待。
2. **align2D：** 保留当前 offset/gain 模型和迭代规则，优化插值、残差与梯度累加的 NEON 实现；旧 `align2D_NEON` 的模型/定点插值与当前实现不同，不能直接打开旧宏。
3. **warpAffine：** 测试行/列坐标复用、整 patch 边界快路径和适合的向量计算；需要保持 floor/插值及失败行为。离散取样限制 SIMD 收益，不能预先承诺倍数。
4. **种子更新调度：** 量化收敛种子反复更新、迭代次数和失败匹配成本，再决定是否减少工作；这类改变需要重新验证精度。
5. **IVE 暂不优先。** 当前全开后 halfSample self 约 2.2%；这段工作即使完全消失也只有约 2.2% 的用户计算时间空间。现有 SDK 的整图 Resize 不适合透明替换这里频繁的小 patch 对齐/仿射取样。

本次没有实施第 2–4 项算法优化；新增的是测量接口、采样/解析脚本和修正后的 benchmark 输出位置。

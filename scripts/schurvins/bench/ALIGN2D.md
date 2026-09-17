# align2D 浮点 NEON 优化

日期：2026-09-17。板端 SG2002 / 单 Cortex-A53 / 800 MHz，GCC 11.3.1，`-O3 -mcpu=cortex-a53`，全静态链接。

## 实现与边界

- 新增独立默认关闭的 `SCHUR_OPT_ALIGN2D_NEON`，宏值为 `1` 才启用，CMake 和 `build.sh` 提供同名开关。
- 在 `feature_alignment.cpp` 的原 align2D 中，按每行 8 像素、每组 4 个 float 并行计算双线性插值和残差。load 的范围仍在原来的 9×9 取样区域内，支持 OpenCV ROI 的非连续 stride。
- 维持 GCC 11 标量路径的 FMA 运算顺序，残差按原像素顺序累加；没有横向向量归约。Hessian、逆矩阵、更新、迭代上限、收敛条件及 `each_step` 接口不变。
- offset 开/关均支持。gain 开启时仍走原标量路径：开发中的全模式向量实现曾出现微小舍入差异，最终版本没有启用它。当前 VIO 默认 offset 开、gain 关，可使用新优化。
- `no_simd=true` 仍强制标量。没有全局开启旧 `__ARM_NEON__` 宏，也不使用旧定点 `align2D_NEON`，没有启用 fast-math。
- 宏未定义或为 `0` 时保留原实现。源码可通过 `0003-a53-align2d-neon.patch` 重放。

## 一致性验证

测试程序独立编译宏关闭的同一源文件为 reference，分别与优化路径、`no_simd=true` 路径比较：返回状态、最终坐标、每一步坐标及迭代次数。

- 32,000 组随机/模糊/平坦/线性梯度/平移及亮度变化图像测试；覆盖四种 offset/gain 组合、0/1/3/10 次迭代、边界与非连续 ROI。
- 3 张真实回放图像，每张 546 个 patch、四种 affine 模式，追加 6,552 组检查。
- 共 **38,552 组全部精确一致**。NaN 结果按双方均为 NaN 判定一致，不比较 NaN payload。
- 精确结果针对当前工具链及编译选项；更换编译器或浮点选项后需要重测。

## 局部耗时

每张真实图像抽取 546 个 patch，初始偏移 `(0.3, -0.25)` 像素，offset 开、gain 关，上限 10 次迭代。热身后测量 7 批，每批对全部 patch 重复 20 次，取中位数。计时调用真实生产函数，标量通过 `no_simd=true` 选择；不包括读图和日志。

| 回放左图 | 标量 μs/次 | 优化 μs/次 | 加速比 |
|---|---:|---:|---:|
| 第 1 张 | 15.8062 | 9.67073 | 1.634× |
| 第 251 张 | 16.1779 | 9.79281 | 1.652× |
| 第 501 张 | 15.7889 | 9.64091 | 1.638× |

这是该 patch 集的局部收益，完整 VIO 收益由独立构建的宏开/关程序测量。

## 完整 VIO 回放收益

同一 EuRoC V1_01 25 秒片段，501 组 752×480 双目图像，排除前 5 帧，统计 496 次 `addImageBundle` 的墙钟时间。无 perf；读图在计时外，日志、位姿和计时写 tmpfs，未修改算法参数。两组均开启之前的 halfSample / ZMSSD / FAST-10 优化，唯一开关差异为 align2D。

按 align → base → align → base 串行测量：

| 配置 | 第一轮平均 ms | 第二轮平均 ms | 两轮均值 ms |
|---|---:|---:|---:|
| align2D OFF | 58.115 | 58.092 | 58.104 |
| align2D ON | 49.428 | 49.415 | 49.421 |

在之前三项优化基础上，VIO 耗时进一步下降 **14.94%**，速度为基线 **1.176 倍**。这是该序列的两轮实测，不是跨场景性能保证。

四次回放均为 `frames=501 tracking=501 imu_fused=501`，全部位姿 CSV 的 SHA-256 与原基线一致：

```text
83e18631b20c8ff3657cb3a4075c1670dfbb87594dfc351f07e6f77b5241e7d2
```

原始逐帧耗时、环境及哈希见 [results-align2d/summary.json](results-align2d/summary.json) 和同目录；完整日志、位姿及可执行文件保留在 `install/schurvins-gcc11-static/align2d-results/` 与 `align2d-binaries/`。

## 产物与复现

运行方法见 [README](../README.md#align2d-浮点-neon独立默认-off)。
`build-align2d.sh` 生成 `align2d-binaries/base`（之前三项 ON，align2D OFF）和
`align2d-binaries/align`（四项 ON）。`run-align2d-check.sh` 执行上述局部检查。
`run-board.sh` 的输出写入 tmpfs，结束后再拉回主机。保持板端无其他计算负载，不在计时期间上传文件。

正式包：`install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static-a53.tar.gz`。
该包显式开启四项优化，但源码开关仍默认 OFF；板端生产安装未被替换。
包内可执行文件与完整回放实测版本的 SHA-256 相同：

```text
7990f7b031a21b302529bbfa2bf6c7211f7277b69ca433533c3f0208d3b2b09f
```

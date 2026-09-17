# align2D Hessian / warpAffine 优化与中间存储评估

日期：2026-09-17。SG2002 / 单 A53 / 800 MHz，GCC 11.3.1，`-O3 -mcpu=cortex-a53`，全静态链接。两项新宏默认 OFF；本次没有实施第 3 项改动。

## 两项独立开关

| 宏 | 开启条件与实现 |
|---|---|
| `SCHUR_OPT_ALIGN2D_HESSIAN_NEON` | gain=false、no_simd=false；8 像素 NEON 整数梯度、int32 平方/交叉乘积累加，精确缩放成原 4×4 H；支持 offset 开/关，其他路径回退 |
| `SCHUR_OPT_WARP_AFFINE_NEON` | 常用 10×10 patch；每批四像素坐标/权重/插值，4+4+2 尾部；其他尺寸、输入输出重叠或异常条件保留标量路径 |

Hessian 的原始差值在 [-255,255]；64×255²=4,161,600，既不溢出 int32，也在 float 精确整数范围内。H 的数值与原实现一致，求逆、更新、终止规则不变。它不依赖之前的插值 SIMD 开关；no_simd=true 同时禁用两项 align2D 优化。

warp 保持原坐标 FMA 后再加中心、floor、w11 连续减法、插值 FMA 和 uint8 截断顺序。边界失败回到原实现重做，保留失败前的部分写入。输入与输出重叠不进入 SIMD 分支，避免提前读取改变原语义。

warp 的初稿使用四个 float 临时数组 gather，虽一致但局部约慢 4%，已淘汰。最终版本按相邻字节对 gather，在寄存器拆分左右像素、完成插值和打包输出；最终宏启用的是这个版本。

## 一致性验证

- align2D：44,800 组人工输入，覆盖四种 affine 模式、0/1/3/10 次迭代、边界、非连续 ROI、随机/模糊/平坦/线性梯度/平移及亮度变化图像，以及两类 0/255 极值梯度图案；三张真实图追加 6,552 组，共 **51,352 组**。
- 同时比较独立编译的宏关闭 reference、当前优化路径、no_simd=true、Hessian ON 且旧 align2D SIMD OFF 四条路径。返回状态、最终坐标、每步坐标及迭代次数精确一致；NaN 按双方均 NaN 处理。
- warp：36,006 组人工/回退检查，覆盖旋转、缩放、剪切、层级、非连续 ROI、整数边缘附近坐标、非 10×10 尺寸、零矩阵与输入输出别名；三张真实图追加 1,500 组，共 **37,506 组**。
- warp 比较返回状态和完整带哨兵输出缓冲区，包含失败时的部分写入。全部逐字节一致。
- 已把五个补丁从 sources.lock 的原始上游版本逐个重放，两个修改后的生产源文件与工作区逐字节相同。

## 完整 VIO 实测

同一 EuRoC V1_01 25 秒片段，501 组 752×480 双目图像；排除前 5 帧，统计 496 次 addImageBundle 墙钟。无 perf，读图在计时外，日志和输出全部写 tmpfs。串行顺序：base → hessian → warp → both → both → base。未在板端计时期间上传文件或运行其他测试。

| 配置 | 平均 VIO ms | 相对四项全开基线耗时下降 |
|---|---:|---:|
| 上一版四项全开 | 49.402 | 0.00% |
| 仅新 Hessian | 45.478 | 7.94% |
| 仅新 warpAffine | 46.283 | 6.31% |
| 新两项全开 | 42.296 | 14.38% |

两轮基线为 49.402 / 49.402 ms，两轮组合版为 42.319 / 42.274 ms。单项各测一轮，组合及基线各两轮；只代表本段回放，不是跨场景性能保证。

六次回放均 `frames=501 tracking=501 imu_fused=501`，所有位姿 CSV 与此前版本的 SHA-256 相同：
`83e18631b20c8ff3657cb3a4075c1670dfbb87594dfc351f07e6f77b5241e7d2`。

三张真实图像的 warp 局部测试最终约 **1.56–1.57 倍**加速（约 9.02 → 5.76 μs/patch），使用独立宏关闭源文件作参考。align2D 当前局部约 5.75–5.87 μs/patch；测试日志中的约 2.88 倍是相对完整标量 align2D，包含上一轮插值 SIMD 的收益，**不能当作此次 Hessian 单项加速比**。本次 Hessian 单项的整体收益以上表为准。

详见 [summary.json](results-next/summary.json)、[最终一致性及局部日志](results-next/both-checks.log) 和同目录逐帧耗时、环境及产物哈希。

## 第 3 项：基于最终汇编的必要性评估

保留了最终目标文件的 [align2D 汇编](results-next/align2d-final.asm)、[warp 汇编](results-next/warp-final.asm) 和对应 [ELF 哈希](results-next/assembly-artifact.json)。下面只检查默认 offset=true、gain=false 的 NEON 迭代路径；函数内保留的标量 fallback 不能误认为默认路径仍在执行。

### 确认存在的中间存储

最终 align2D 起始地址为 `0x4adeb0`，栈帧大小 `0x380`（896 字节）。默认循环中：

```text
4ae2a8: add x2, sp, #0xe0     // residuals 的低四个 float
...
4ae374: str q1, [x2]         // 写 16 字节残差
4ae378: ldr s2, [sp, #224]   // 随即读第一个残差
4ae37c: str q0, [sp, #240]   // 写另外 16 字节
...
4ae3a8: ldp s20, s19, [sp, #228]
...                          // 其余残差也通过 ldr/ldp 读回
```

每行 32 字节 store + 32 字节 load，8 行即每次迭代约 **512 字节显式栈访问**。这不等于 512 字节 DRAM 流量：这是小型栈工作集，不能仅凭汇编判断 cache miss。累加器也在行边界读写栈，但行内主要是寄存器中的有序 FMA 链。

### 编译器已经做了什么

- 八像素累加已经展开，不能再把手工展开作为独立收益来源。
- 标量顺序累加已经使用 FMA；换 intrinsic 不能默认再减少乘加指令数。
- 梯度/Hessian 的向量分支已经使用整数 widening multiply-accumulate 和归约。相邻的旧矩阵循环属于 fallback。
- 模板 ref_patch 的 widening / uint→float 仍在每行、每次迭代执行，并没有自动全部外提。

### 判断

**残差寄存器化值得做小范围、保序的下一轮实验，但尚不能证明收益足够大，当前不实施。** 两个残差向量留在寄存器后仍需按原像素顺序访问 lane；额外 lane 提取、寄存器占用、可能新增的 spill，会抵消去掉 L1 store/load 的部分收益。默认循环已经使用较多向量寄存器，还承载权重、alpha/offset、坐标、梯度和累加器；不能只看源代码数组就假定删除后会更快。

**模板提前转 float 暂不优先。** 64 字节模板会变成 256 字节；需要额外准备、栈空间和更大的加载量。是否划算取决于实际迭代次数。当前没有证明模板转换是主要瓶颈，也没有独立阶段计时支持该变更。

若继续第 3 项，优先将默认 affine 模式的热循环与 fallback 分离、保持简单寄存器累加器，再尝试仅移除 residuals 的中间存储，保持像素累加顺序和 affine fallback；要求最终汇编确实减少热路径访问且不新增更多 spill，再用原有逐步一致性和完整回放验证。不要改成四路独立浮点和后水平归约，也不要混入降维求解。

## 复现与产物

方法见 [README](../README.md#align2d-hessian-与-warpaffine各自默认-off)。
`next-binaries/base` 是之前四项 ON、新两项 OFF；hessian / warp / both 为只开新 Hessian、只开新 warp、两项都开。使用 `run-board.sh` 串行回放，所有计时/日志/位姿写 tmpfs。

原始结果保存在 `install/schurvins-gcc11-static/next-results/`，冻结二进制与同次链接符号在 `next-binaries/`。首次基线直接使用之前已经验证的四项全开 ELF；其余变体以本次源码构建。

正式包路径为 `install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static-a53.tar.gz`，显式开启全部六项宏。板端生产安装未替换。

正式包采用冻结并上板验证过的 `next-binaries/both/schurvins_stereo`，SHA-256：
`5a13db1c693c5248b3f065b440abcd3b7d5cf60fca187a1fd724f8b893212de3`。
完整 build.sh 重链接的文件与它仅有 7 字节 OpenCV 构建信息中的 Git 版本字符串不同；为让交付与测试完全对应，打包时使用了上述已测文件并更新包内 SHA256SUMS/BUILD-INFO。

# warpAffine 两像素尾部优化

## 宏开关

`SCHUR_OPT_WARP_TAIL2` 默认 OFF，开启时需同时开启 `SCHUR_OPT_WARP_AFFINE_NEON`。
10 像素行的最后两点使用双通道 NEON，避免四通道冗余计算。
保留边界失败后重走标量路径及部分输出写入语义。

## 数值验证

清理前的八组实验组合均通过以下验证，保留的 tail 组合包含在内：

- align2D：44,800 个合成案例，以及两幅实拍图对应的案例，总计 47,792 个；返回状态、位置和每轮迭代轨迹精确一致。覆盖四种仿射模式、ROI 步长、边界、0/1/3/10 轮、平坦/斜坡/随机/模糊图像。
- warpAffine：36,006 个合成案例，以及实拍图案例，总计 36,754 个；状态及完整输出缓冲逐字节一致，包含部分写入、哨兵、ROI、层级、尾部和输入输出重叠回退。
- 六组完整回放（base/ref/cell/contiguous/tail/all）均为 `frames=744 tracking=744 imu_fused=744`，本轮全部位姿 CSV 与 base 数值最大差为 0。

该精确一致性是本次受控组间结果，不代表此前发现的实时/回放差异已经被修复。

## 历史对比：移除无收益的实验

以下为清理前测量记录。参考块缓存、整数格缓存、连续加载的实现和开关均已删除；
仅保留两像素尾部优化。

## 微基准

使用录制约30秒处的左右图，各374个patch；每组8批，首批预热，取余下批次中位数，表中为左右图结果平均。
base 为之前六项 NEON 优化全部开启、新四项关闭。

| 新开启项 | align2D µs/调用 | warpAffine µs/调用 |
| --- | ---: | ---: |
| base | 4.566 | 4.335 |
| REF_CACHE | 4.865（慢6.6%） | 4.312 |
| CELL_CACHE | 5.295（慢16.0%） | 4.316 |
| CONTIGUOUS | 4.514 | 4.370（慢0.8%） |
| TAIL2 | 4.519 | **3.128（减少27.8%）** |
| 四项全开 | 5.274 | 3.780 |
| 两项align缓存 | 5.263 | 4.403 |
| 两项warp优化 | 4.579 | 3.735 |

缓存增加float数组写入/加载、判断及寄存器压力，当前版本未获得净收益。
连续地址检查与查表开销也抵消了加载减少的收益；上面只是测量结果，没有单独分摊这些开销。

## 完整回放

计时范围为 `vio.addImageBundle()`，不包括SD读图。日志、计时、位姿均写/tmp；未运行实时相机或录制。

| 新开启项 | 平均 ms/帧 | P95 ms | 15秒后运动段平均 ms |
| --- | ---: | ---: | ---: |
| base | 24.376 | 33.930 | 27.430 |
| REF_CACHE | 24.756 | 35.176 | 27.815 |
| CELL_CACHE | 25.053 | 35.232 | 28.270 |
| CONTIGUOUS | 24.459 | 34.265 | 27.580 |
| TAIL2 | **23.651** | **33.145** | **26.616** |
| 四项全开 | 24.960 | 35.490 | 28.127 |

TAIL2 首轮整帧平均耗时减少2.97%，累计VIO耗时18.136→17.596秒。
反序复测（TAIL2后base）：23.663 vs 24.238 ms/帧，减少2.37%；两轮平均
base 24.307、TAIL2 23.657 ms/帧，减少约2.67%。所有复测位姿仍与本轮base逐数值一致。
测试结束仍为1GHz，温度约40.95°C。不是整个录像回放耗时减少27.8%，也不解决SD写盘积压。

## 构建与复测

推荐构建（保留原六项优化，仅启用新增TAIL2）：

```sh
SCHUR_WITH_MICRO_ROS=ON SCHUR_BUILD_BENCHMARKS=ON \
SCHUR_OPT_HALFSAMPLE_NEON=ON SCHUR_OPT_ZMSSD_NEON=ON \
SCHUR_OPT_FAST10_NEON=ON SCHUR_OPT_ALIGN2D_NEON=ON \
SCHUR_OPT_ALIGN2D_HESSIAN_NEON=ON SCHUR_OPT_WARP_AFFINE_NEON=ON \
SCHUR_OPT_WARP_TAIL2=ON \
  scripts/schurvins/build.sh
```

运行时仍可 `SCHUR_MICRO_ROS=0`；不需要ROS Agent。

构建 base/tail 两组独立对照产物（依赖上面的core和benchmark目标已构建）：

```sh
python3 scripts/schurvins/bench/build-warp-tail-variants.py \
  --output "$PWD/install/cache-opt-variants"
```

每组包含 `schurvins_stereo`、`schurvins_warp_bench` 和 `options.txt`。
脚本单独编译/替换静态库的 patch_warp 目标文件，不替换生产程序。
板端单组测试示例：

```sh
./schurvins_warp_bench /path/to/left.pgm /path/to/right.pgm
SCHUR_MICRO_ROS=0 SCHUR_TIMING_CSV=/tmp/timing.csv ./schurvins_stereo \
  --replay /mnt/data/vio-record-20260924-121320 /tmp/poses.csv --allow-incomplete
```

`--allow-incomplete` 仅用于这份已有队列满标记的录制，读取其已写前缀。
原始数据和本次产物：`install/vio-analysis-20260924-121320/cache-opt-results/`、
`cache-opt-variants/`、`cache-opt-selected/`。诊断前置结果见同目录的 `align-warp-audit/`。

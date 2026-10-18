# 实际硬件向量 + 两层 LK + 完整金字塔补跑

后续对补跑有效率、失败原因和取消补跑的验证见 [RETRY.md](RETRY.md)。
同一段录像的硬件 H.264 完整分区向量对照见 [H264.md](H264.md)。
H.265 向量通过轻量验证后直接跳过 LK 的实验见 [SKIP-LK.md](SKIP-LK.md)。

## 本轮实现

实验使用 SG2002 **实际 H.265 编码导出的向量**，分别编码完整的：

- TideSense `dataset-20261012-084638`：885 帧，640×400，200 Hz IMU。
- UZH-FPV `indoor_forward_6`：1970 帧，640×480，约 30 Hz 图像、500 Hz IMU。

编码为 Normal-P、GOP 60、固定 QP 20；逐帧核对原始纳秒时间戳与编码 PTS。
每 60 帧的 I 帧没有向量。板端直接读取 `mvCol`，不运行 FFmpeg。
缓存保留原始时间戳和前一输入帧时间戳；本机、A53 使用同一份硬件向量。
LK 使用原始灰度图，经原有直方图均衡处理；不使用有损解码图。

保持 OpenVINS 单目、150 点、16×15 窗口、FP64 后端、原有标定和噪声。
LK 的迭代上限仍为 30、终止阈值为 0.01；RANSAC 仍为 1000 次上限、置信度 0.999。

| 实验名 | 运动初值 | LK |
|---|---|---|
| `base` | 原始零位移 | 完整金字塔 |
| `mvfull` | 硬件块向量 | 完整金字塔 |
| `shallow` | 硬件块向量 | 先跑第 0、1 层，失败点完整补跑 |

两层方案的具体策略：

1. 只有拿到向量初值的点进入浅层；无向量的点直接完整 LK。
2. 浅层结果要求 LK 成功、坐标有限且在图像内、平均窗口 L1 残差不超过 20 灰度级，
   精细位置与向量初值的距离不超过 6 px。
3. 未通过的点采用零位移初值，完整金字塔补跑一次。
4. 恢复原始特征点顺序和 ID，再统一执行原有归一化及几何剔除。

本轮保留所有预计算的图像金字塔与梯度，仅减少一部分点实际执行的 LK 层数。
宏 `OPENVINS_CODEC_MV_SHALLOW` 隔离这部分代码；独立实验构建定义为 1，
运行开关 `OV_CODEC_MV_LK` 默认 `full`。没有接入默认 VIO 构建。

## 本机精度

每组完整运行两次，相同配置的所有重复位姿数值一致。关闭向量的两份录像与此前本机
基线的全部位姿数值完全一致。各组初始化后输出帧数保持 TideSense 647、FPV 1782。

| 数据集及指标 | base | mvfull | shallow |
|---|---:|---:|---:|
| FPV 位置 ATE RMSE，m | 0.2387 | 0.2599 | 0.2445 |
| TideSense 首末位置差，m | 1.021 | 2.746 | 1.891 |

FPV ATE 采用各版本共同的 816 个真值时间戳，约 30.15 秒，SE(3) 对齐，不拟合尺度；
相机到真值时间换算保留官方 -16.684572 ms。真值没有覆盖完整输入序列。
TideSense 无外部真值，首末距离只是返回起点录像的漂移参考，不是 ATE。

本机 shallow 的向量覆盖率及补跑情况：

| 数据集 | 获得向量的点 | 浅层尝试中补跑的点 | 总点数中需要完整 LK 的点 |
|---|---:|---:|---:|
| FPV | 66.3% | 4.6% | 36.7% |
| TideSense | 28.6% | 18.3% | 76.7% |

补跑率的分母只包含尝试过浅层的点；不能据此认为 95% 的全部特征都绕过了完整 LK。
上述统计仅包含点数达到 10、进入 LK 的调用；原有低点数提前返回逻辑保留。

## A53 实测：FPV

A53 单核 1 GHz，每组完整回放一遍：

| 方案 | 原生前端 ms | 向量缓存准备 ms | 前端合计 ms | LK ms | 整帧 VIO ms | ATE m |
|---|---:|---:|---:|---:|---:|---:|
| base | 25.877 | 0.003 | 25.880 | 8.120 | 51.010 | 0.2464 |
| mvfull | 25.882 | 1.146 | 27.027 | 8.061 | 51.886 | 0.2468 |
| shallow | 24.490 | 1.145 | 25.635 | 6.505 | 52.643 | 0.2554 |

浅层方案的 LK 部分减少约 20%，原生前端减少约 5.4%；计入向量缓存准备后，前端只减少
约 0.24 ms、0.9%。这个微小差异不应当作稳定的整体收益。
整帧平均耗时反而增加约 3.2%。各方案的观测集合和后端更新工作也会随跟踪结果改变。

单独的硬件采集记录：FPV 送帧到码流就绪平均 2.934 ms，向量获取解析平均 0.557 ms。
这些开销尚未计入上表。按串行成本估算，shallow 前端约为
`25.635 + 2.934 + 0.557 = 29.126 ms`，高于基线 25.880 ms。
缓存准备含有本可避免的文件读取，编码与预处理也可能重叠，所以这个相加值不是实时接口的实测值。
当前实验没有显示值得接入正式前端的净收益。

FPV 初始化后的 1782 次匹配调用中，仅 2 次完全不需要完整 LK。
因此按需生成深层金字塔，在当前覆盖率下也很少有机会省掉整帧的深层构建。

## A53 实测：TideSense

| 方案 | 原生前端 ms | 向量缓存准备 ms | 前端合计 ms | LK ms | 整帧 VIO ms | 首末距离 m |
|---|---:|---:|---:|---:|---:|---:|
| base | 58.909 | 0.003 | 58.912 | 12.297 | 79.282 | 1.251 |
| mvfull | 57.863 | 0.560 | 58.423 | 12.359 | 79.135 | 2.537 |
| shallow | 58.126 | 0.555 | 58.681 | 12.009 | 78.752 | 0.425 |

浅层方案约 28.2% 的点得到向量，其中 18.3% 补跑；约 77.0% 的总点数仍执行完整 LK。
LK 只减少约 0.29 ms，计入缓存后的前端减少约 0.23 ms、0.4%。原生 RANSAC 平均仍需约
31.6 ms，是本录像的主要前端开销。

硬件送帧到码流就绪平均 2.692 ms，向量获取解析平均 0.481 ms；这些也未计入上表。
A53 基线的全部位姿数值与此前同平台基线完全一致。

A53 首末距离改善，但本机同一方案从 1.021 m 变为 1.891 m，方向相反。
两个平台的原始基线本身也不同；这些结果不能当作同一轨迹的重复精度测量。
缺乏全程真值，不能凭单个终点断言本方案稳定提高了精度。

## 当前结论

两层 LK 在 FPV 上确实降低了 LK 计算量，但缓存准备消耗了大部分前端收益；
在 TideSense 录像上的节省更小。两份录像的前端合计仅减少约 0.23–0.24 ms，
额外的编码、获取与解析仍是数毫秒量级。**本轮不建议默认开启**。

本机每组重复两遍用于检查精度复现，A53 每组完整运行一遍。
本轮没有继续调小迭代上限、修改噪声、改变特征上限或实现按需构建金字塔。

## 计时口径

- 本机精度验证后，A53 单核 1 GHz、单 OpenCV 线程、无 ROS，按序处理全部输入帧。
  不按墙钟节流，也不丢帧。
- 原生 `tracking` 时间计入图像预处理、金字塔和梯度、检测、查询初值、LK、几何剔除及
  前端诊断；不含回调前的向量缓存读取、建索引。
- 汇总中的 `effective_frontend_ms` 将原生前端与向量缓存准备时间相加。
- 相机回调时间还包含原有 VIO 后端以及缓存准备；不含图像文件读取及回调外 IMU 输入。
- 统计初始化后状态，略去首次初始化帧。原生计时行数与初始化后状态行数核对。
- 回放使用预先导出的向量，**这些耗时不包含硬件编码及向量提取**。
  `hardware-timing.json` 单独记录它们；将两者相加只能得到串行成本估算，
  不是已实现的实时摄像头流水线测量。也没有实测编码与图像预处理重叠的收益。

## 复现

所有新构建与结果位于 `install/openvins-codec-shallow/`，不覆盖此前实验。
以下电脑端命令均在工程根目录执行。

```bash
python3 scripts/vio-compare/probes/codec-mv/shallow_run.py --prepare-manifests
python3 scripts/vio-compare/probes/codec-mv/build_device.py \
  --out install/openvins-codec-shallow/device
python3 scripts/vio-compare/probes/codec-mv/build.py --caps 1000 \
  --out install/openvins-codec-shallow/host
python3 scripts/vio-compare/probes/codec-mv/build.py --platform a53 --caps 1000 \
  --out install/openvins-codec-shallow/a53
```

硬件缓存采集：

1. 运行时板上需没有其他多媒体程序。复制 `sample_venc_probe`、`cleanup_idle`、
   两份 `frames.txt`、`collect_hardware.sh` 至 `/mnt/data/openvins-codec-shallow/` 对应目录。
2. FPV 图片复用 `/mnt/data/public-vio-benchmark/uzh-fpv-indoor6/img/`。
   TideSense 左图复制至 `/mnt/data/openvins-codec-shallow/tide-record/left/`。
   图像清单由 `--prepare-manifests` 生成。
3. 板端执行 `sh collect_hardware.sh`，回传 `tide/vectors.bin`、`fpv/vectors.bin` 及编码日志。
   sparse YUV 仅用于 SDK sample 的帧数检查；实际逐帧读取清单中的原始 PGM，不生成完整 YUV 数据集。
4. 采集程序复用 MV 映射，P 帧以哪个槽发生变化进行核对；静止未变化时采用已观察的交替关系。
   该方式仍为单通道实验私有缓冲区读取，正式驱动接口应提供 `reconFrameIndex` 和参考信息。

本机精度测试及统计：

```bash
python3 scripts/vio-compare/probes/codec-mv/shallow_run.py --repeat 2
/usr/bin/python3 -S scripts/vio-compare/probes/codec-mv/shallow_summary.py
```

A53 回放：

1. 将 `a53/run_record.r1000.stripped` 部署为实验 `run_record`，并部署 `a53-config-*`。
   `shallow_run.py --prepare-a53` 生成启用原生计时的配置。
2. 为两个 `*-record` 准备 `stereo.csv`、`imu.csv` 及左图。
   **复制本机元数据时需解引用符号链接**，例如 `tar -h`；保留本机原始时间戳。
   FPV 图片可以使用指向原图目录的符号链接。
3. 板端执行 `sh shallow_a53.sh 1`，日志写文件；回传 `a53-results/` 后运行统计脚本。

主要文件：

- `summary.json`：逐组精度、计时、覆盖率和补跑率。
- `aggregate.json`：重复测试的汇总。
- `hardware-timing.json`：硬件编码与读取解析耗时。
- `host-results/`、`a53-results/`：原始轨迹、逐帧前端指标、原生计时和运行日志。

本次结束后清理了板上 `tide-record/left/` 的临时原图副本以释放 SD 空间；
原始压缩录像及本机解压数据保留。重复 TideSense 测试前需按清单恢复该目录。

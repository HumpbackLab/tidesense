# OpenVINS FP32 backend experiment

参考 `/home/ncer/fpga_vio/tools/prepare_openvins_fp32.py`，将 OpenVINS v2.7
的 `ov_msckf/src/state` 和 `update` 中的 Eigen 矩阵转换为 FP32。
默认不启用，生成独立源码和构建目录，不替换设备正在使用的程序。

## 转换范围与开关

- 状态协方差、传播矩阵、视觉 Jacobian、残差、增益及矩阵求解使用 FP32。
- 时间戳、`ov_type` 的名义状态存储、相机模型接口及 Ceres 初始化保持 FP64。
  并非所有后端标量运算都变成 float，例如 Givens rotation 系数仍为 double。
- 保留 MSCKF/SLAM 的 chi-square 异常观测剔除、原来的滑窗和初始化参数。
  未移植参考目录中删除剔除、缩短滑窗及替换 QR 算法的其他改动。
- 传播和边缘化后显式对称化协方差；不截断负值、不关闭数值检查。
- 生成源码要求编译宏 `OPENVINS_FP32_BACKEND=1`。同一宏和 Eigen 混合类型
  头文件也传递给调用程序，保证 `State` 的 ABI 一致。不要混用 FP64 的库和 FP32 头文件。
- 完整 FP32 内的 `P Hᵀ` 打包默认关闭，运行时设置
  `OPENVINS_FP32_PACKED_MA=1` 才使用打包乘法，便于同一程序对照。

`eigen_mixed.h` 允许 Eigen 的 float/double 混合表达式缩窄为 float。
这是一项较宽的精度转换实验，不应视为经过数值验证的生产后端。

## 构建

在 SDK 根目录执行：

```bash
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 mixed
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp32
# 本地对照
scripts/vio-compare/probes/backend-f32/build_variant.sh host fp64
scripts/vio-compare/probes/backend-f32/build_variant.sh host fp32
```

该包装脚本从 `install/vio-compare-20260924-121320/openvins/open_vins` 的
Git HEAD 导出干净源码，然后应用维护的 offline/headless 补丁。
不带入工作树中的 XFeat/TPU 修改。输出位于
`install/openvins-backend-f32/{a53,host}-{fp64,mixed,fp32}/build/`。
依赖本 SDK 已下载的 Boost/OpenCV/Ceres 和
`install/fisheye-benchmark/openvins/run_fisheye.cpp`，不是独立安装包。

已有维护源码的常规构建入口也支持：

```bash
OPENVINS_FP32_BACKEND=1 OPENVINS_SKIP_ACTIVE_VIZ=ON \
  scripts/vio-compare/build.sh openvins
```

该入口生成 `openvins/fp32_source` 和 `openvins/build-a53-fp32`。
不能同时设置 `OPENVINS_EKF_MA_EXPERIMENT=1`。关闭完整 FP32 时使用原来的
源码与构建目录。若维护工作树有其他实验改动，优先用上面的独立包装脚本。

## 回放与数值检查

```bash
OPENVINS_NUMERIC_EIGEN=1 OPENVINS_FP32_PACKED_MA=1 \
  install/openvins-backend-f32/a53-fp32/build/run_record \
  config/estimator_config.yaml record_dir trajectory-fp32packed.csv 744
```

A53 可执行文件需要复制到设备上执行。`numeric_probe.h` 在计时范围外检查
协方差有限性和非负对角项，逐帧记录维度、最小对角项和非对称程度；
`OPENVINS_NUMERIC_EIGEN=1` 每 50 帧用双精度求最小特征值。
小的负特征值也如实保存，不进行钳位。

`prepare_configs.py` 为每个模式复制同一份标定和选项，只改变 stage 计时文件路径。
在板端实验目录准备 `run_record.mixed`、`run_record.fp32`、
`run_record.fp32packed` 和对应配置后可运行：

```bash
sh run_a53.sh /mnt/data/vio-compare/backend-f32 mild \
  /mnt/data/vio-record-20260924-121320 744 'original mixed fp32 fp32packed'
```

`original` 和 `mixed` 使用同一个 mixed 程序，分别选择原始乘法和局部 FP32 打包；
`fp32` 与 `fp32packed` 均可使用同一份完整 FP32 程序，通过环境变量切换打包。
脚本使用板端回放锁，保存日志和退出码。取回结果后：

```bash
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/board-results/results-mild \
  --variants original mixed fp32 fp32packed
```

比较器报告完整/部分回放和共同时间戳上的耗时、位姿差。异常退出时，CSV
可能因流缓冲丢失最后几十行，不能用其行数推断故障帧；应结合逐帧刷新的 stage
文件与错误日志。失败运行的均值不是完整数据集的吞吐成绩。

## 2026-10-02 测量

源码基于 `6948812`；A53 为 SG2002 1 GHz、GCC 11.3、Eigen 3.4、
OpenCV 3.4.20、静态程序、关闭 ROS、关闭 active visualization、单 OpenCV 线程。
每段数据各模式的参数相同，保留原始异常观测剔除。
以下单次测量不包含图像读取、CSV/日志写入、额外数值检查。
后端均值来自初始化后的 propagation、MSCKF、SLAM、delayed init、marginalization
计时之和；整帧均值包括初始化前帧，不能把两列直接相减。

### A53 普通录像：`vio-record-20260924-121320`

全部 744 帧跑完，417 帧初始化后输出，没有负对角项或非有限值。
录像没有真值，位姿差只是与 FP64 的数值一致性。

| 模式 | 整帧均值 ms | 初始化后整帧 ms | 后端均值 ms | 末帧位置差 | 末帧姿态差 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 原始 FP64 | 51.25 | 75.24 | 34.11 | — | — |
| 局部 FP32 打包，协方差 FP64 | 47.15 | 68.04 | 27.06 | 0.012 mm | 0.00030° |
| 完整 FP32，原始分块乘法 | 47.89 | 69.19 | 27.78 | 0.142 mm | 0.00521° |
| 完整 FP32 加打包 | 44.31 | 62.76 | 21.70 | 0.348 mm | 0.00237° |

组合方案后端减少 36.4%，整帧减少 13.5%；比已有局部 FP32 的整帧再减少 6.0%。
FP32 对称化使 `re-tri & marg` 从约 1.12 ms 增加到 2.12 ms；主要收益来自
SLAM update 和 delayed init。跟踪仍约 40 ms，完整 FP32 没有解决前端瓶颈。
初始化后整帧 62.76 ms，仍不能据此声称实时 20 Hz。

完整 FP32 的最小抽样特征值约为 -1.9e-9，FP64 约 -3.1e-18。
即使对角项通过，也不能宣称协方差严格半正定。

### A53 强运动：`dataset-20260924-174756`，前 800 帧

| 模式 | 结果 | 完整回放整帧均值 |
| --- | --- | ---: |
| 原始 FP64 | 800 帧结束，但轨迹已经发散 | 87.50 ms |
| 局部 FP32 打包 | 800 帧结束，轨迹同样发散 | 86.82 ms |
| 完整 FP32 | 第 476 帧为最后完成帧，随后负协方差退出 | 不可作为完整成绩 |
| 完整 FP32 加打包 | 第 479 帧为最后完成帧，随后负协方差退出 | 不可作为完整成绩 |

完整 FP32 在约 24 秒处触发 `StateHelper::EKFUpdate` 检查，原始分块版本
有对角项达到约 -0.02。`P -= K Mᵀ` 的减法更新、求解和较大动态范围在 FP32 下
存在舍入/抵消风险；这是原理上的解释，尚未逐项定位是哪一步首先产生误差。
打包并不能解决这个数值故障。FP64 末位置约 (-112, -594, -17) m，
说明此配置下原始算法也未正确跟踪，不能把完整 FP32 的崩溃当作这段录像发散的唯一原因。
前 392 个共同初始化后时间戳上的整帧均值为 FP64 74.19 ms、局部 FP32 74.11 ms、
完整 FP32 73.63 ms、完整 FP32 加打包 74.26 ms；此片段也没有明确的整帧收益。

### 本地 UZH-FPV：`indoor_forward_6`

同一配置原生初始化，1970 帧全部完成，1782 帧初始化后输出。
使用双目鱼眼输入和 500 Hz IMU；GT 时间偏移 -16,684,572 ns，
在相同的 816 个真值覆盖时间戳上分别做无尺度 SE(3) 对齐。

| 模式 | 整帧均值 ms | 后端均值 ms | ATE RMSE |
| --- | ---: | ---: | ---: |
| 原始 FP64 | 6.67 | 2.24 | 0.238 m |
| 完整 FP32 | 5.96 | 1.70 | 0.216 m |
| 完整 FP32 加打包 | 5.52 | 1.22 | 0.226 m |

组合方案后端减少 45.7%，整帧减少 17.2%。但末帧与 FP64 的位置差为 7.1 cm，
姿态差为 0.289°；不打包 FP32 的末位置差为 24.8 cm。
单段 ATE 略低不能证明 FP32 更准确。完整 FP32 的最小抽样协方差特征值约 -1.2e-7。

```bash
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/host-uzh/results-uzh \
  --variants original fp32 fp32packed \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --shift-ns -16684572
```

### 本地 UZH-FPV：局部 FP32 补测

在同一个 `host-mixed/build/run_fisheye` 程序中，用
`OPENVINS_EKF_MA_MODE=original|packed_float` 切换。数据、标定、噪声、
单线程及 GT 评价方法与上一节相同。两种模式均完成 1970 帧，1782 帧初始化后输出，
退出码为 0。补测 FP64 的轨迹与上一轮 FP64 的所有非耗时 CSV 字段完全一致。

| 模式 | 整帧均值 ms | 整帧 p95 ms | 后端均值 ms | ATE RMSE |
| --- | ---: | ---: | ---: | ---: |
| 原始 FP64 | 6.60 | 11.42 | 2.22 | 0.238 m |
| 局部 FP32 打包，协方差和求解保持 FP64 | 5.72 | 10.61 | 1.52 | 0.223 m |

单次测量整帧减少 13.4%，后端减少 31.8%。ATE 没有变差，但不代表 FP32 普遍更准确。
局部版本与 FP64 的共同初始化帧位置差 RMS 为 0.141 m、p95 为 0.253 m、
最大为 0.319 m，末帧差为 0.218 m；姿态差 p95 为 0.645°、末帧为 0.162°。
因此不能把普通录像上的微米级数值一致性推广到这段强运动数据。

没有触发负协方差对角项或非有限值检查，局部版本最小抽样特征值为 -1.26e-10
（原始约 -7.59e-17）。协方差保留 FP64 有助于降低风险，但局部乘法依然会改变数值路径，
没有严格半正定保证，也尚未验证全部 UZH-FPV 序列。

复现与结果目录：

```bash
scripts/vio-compare/probes/backend-f32/build_variant.sh host mixed
bash install/openvins-backend-f32/host-uzh-mixed/run.sh
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/host-uzh-mixed/results-uzh \
  --variants original mixed \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --shift-ns -16684572
```

`run.sh` 是此次实验保存在 install 下的执行记录；配置和原始日志、CSV、退出码、
`comparison.json` 同样位于 `install/openvins-backend-f32/host-uzh-mixed/`。
此处是本地 PC 结果，不是 A53 耗时。

### 在 A53 部署 UZH-FPV 局部 FP32 对照

为了保留设备 SD 空间，用 `prepare_images.py` 将 1970 对 PGM 转为无损 PNG。
所有 3940 张图像重新解码后逐像素相等；保留全部时间戳，IMU 文件逐字节复制。
打包约 622 MiB，CSV 使用 Unix 换行以兼容回放程序。
此脚本需要本机 Python 的 OpenCV 和 NumPy。

```bash
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 mixed
python3 scripts/vio-compare/probes/backend-f32/prepare_images.py \
  install/fisheye-benchmark/openvins/uzh-input \
  install/openvins-backend-f32/uzh-board-input
python3 scripts/vio-compare/probes/backend-f32/prepare_configs.py \
  install/openvins-backend-f32/host-uzh/config-uzh-original \
  install/openvins-backend-f32/board-config \
  --name uzh --variants original mixed \
  --runtime-root /mnt/data/vio-compare/backend-f32
```

程序部署为 `/mnt/data/vio-compare/backend-f32/run_fisheye.mixed`，
输入为该目录下的 `uzh-input/`，两份配置为 `config-uzh-{original,mixed}/`。
设备文件用打包目录中的 `SHA256SUMS` 校验。两种模式用同一个静态 GCC 11.3 程序。
在设备上运行：

```sh
cd /mnt/data/vio-compare/backend-f32
sh run_fisheye_a53.sh /mnt/data/vio-compare/backend-f32 \
  /mnt/data/vio-compare/backend-f32/uzh-input 0
# 完整 FP32 的两种乘法路径，先部署 run_fisheye.fp32 和对应配置：
sh run_fisheye_a53.sh /mnt/data/vio-compare/backend-f32 \
  /mnt/data/vio-compare/backend-f32/uzh-input 0 'fp32 fp32packed'
# 单独运行局部 FP32 模式：
OPENVINS_EKF_MA_MODE=packed_float OPENVINS_NUMERIC_EIGEN=1 \
  ./run_fisheye.mixed config-uzh-mixed/estimator_config.yaml \
  uzh-input trajectory-mixed.csv 0
```

图像解码和数值诊断在 camera-call 计时之外。整体 wall time 包含 PNG 解码、
IMU 调用、日志写入和诊断，不能用 camera-call 均值直接声称实时输入吞吐。
取回设备 `results-uzh/` 后，按本地同样的 GT 时间偏移与 SE(3) 对齐评价：

```bash
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/board-results/results-uzh \
  --variants original mixed \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --shift-ns -16684572
```

本次 A53 1 GHz 实测，两种模式均完成 1970 帧，1782 帧初始化后输出，
退出码为 0；使用共同的 816 个 GT 覆盖帧评价。完整结果如下：

| 指标 | 原始 FP64 | 局部 FP32 打包 |
| --- | ---: | ---: |
| 整帧平均耗时 | 89.53 ms | 83.67 ms |
| 初始化后整帧均值 | 95.85 ms | 89.37 ms |
| 整帧 p95 | 174.15 ms | 156.60 ms |
| 整帧最大耗时 | 374.71 ms | 473.29 ms |
| 后端平均耗时 | 36.53 ms | 29.65 ms |
| ATE RMSE | 0.223 m | 0.214 m |
| 整段 wall time（含解码和数值诊断） | 278.86 s | 267.79 s |

整帧均值减少 6.5%，后端均值减少 18.8%，整体回放时间减少约 4.0%。
平均和 p95 有收益，但单帧最大值没有改善。主要后端收益在 SLAM update
（15.98 → 11.41 ms）和 delayed init（10.64 → 8.68 ms）。
跟踪均值仍约 59 ms，处理初始化后的一帧仍约 89 ms，不满足实时 20 Hz。
本次 PNG 回放包含逐帧数值检查及每 50 帧一次的协方差特征值求解，
其 wall time 不能直接推广到关闭诊断、实时采集的应用。

局部版本与 FP64 的位姿差 RMS 为 0.059 m、位置差 p95 为 0.101 m，
末帧位置差为 0.099 m、姿态差为 0.260°。
没有触发负对角项或非有限值检查，但最小抽样协方差特征值约 -8.4e-11
（原始 -1.95e-16），不保证严格半正定。单段 ATE 略低不表示普遍精度提升。
此结果属于 A53 对照，不应直接套用前一节 PC 的 31.8% 后端收益。

两轮原始日志、trajectory/stage CSV、退出码和 `comparison.json` 位于
`install/openvins-backend-f32/board-results/results-uzh/`。
程序与数据保留在设备上，可以用上面的命令再次运行。

### A53 UZH-FPV：完整 FP32 补测

与上一节的 FP64 和局部版本使用相同输入、配置和数值诊断，仍为 A53 1 GHz、
无 ROS、单 OpenCV 线程、保留异常观测剔除。程序部署为
`/mnt/data/vio-compare/backend-f32/run_fisheye.fp32`，两种完整 FP32 路径用同一程序
及 `OPENVINS_FP32_PACKED_MA=0|1` 切换。两种均完成 1970 帧、1782 帧初始化后输出，
退出码为 0，没有触发负协方差对角项或非有限值检查。
四种模式的 ATE 均使用共同的 816 个 GT 覆盖时间戳：

| 模式 | 整帧平均 ms | 初始化后整帧 ms | 后端平均 ms | ATE RMSE |
| --- | ---: | ---: | ---: | ---: |
| 原始 FP64 | 89.53 | 95.85 | 36.53 | 0.223 m |
| 局部 FP32 打包 | 83.67 | 89.37 | 29.65 | 0.214 m |
| 完整 FP32，原始分块乘法 | 83.02 | 88.67 | 29.66 | 0.219 m |
| 完整 FP32 加打包 | 76.54 | 81.47 | 22.41 | 0.228 m |

完整 FP32 加打包相对原始减少 14.5% 的整帧均值和 38.6% 的后端均值；
相对局部版本，分别再减少约 8.5% 和 24.4%。不打包的完整 FP32 后端与局部
FP32 耗时几乎相同。打包的主要收益在 SLAM update（原始 15.98 → 7.15 ms）
和 delayed init（10.64 → 5.99 ms）。跟踪仍约 58 ms，初始化后整帧约 81 ms，
未满足实时 20 Hz。

| 完整 FP32 路径 | 整帧 p95 | 整帧最大值 | wall time（含解码和诊断） | 末位置差 vs FP64 | 末姿态差 vs FP64 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 分块 | 153.50 ms | 376.42 ms | 265.68 s | 0.214 m | 0.785° |
| 打包 | 144.17 ms | 360.45 ms | 252.99 s | 0.253 m | 0.207° |

分块路径的位置差 RMS 为 0.096 m，打包为 0.135 m。
ATE 与 FP64 接近，本段未出现明显轨迹精度恶化；不能根据单段数据认定所有场景稳定。
最小抽样协方差特征值分别为 -3.87e-7 和 -8.05e-8，远大于 FP64 的负值量级，
尽管对角项通过检查，完整 FP32 仍不保证严格半正定。
此前自录 `dataset-20260924-174756` 的负协方差退出依然存在；它与此处 UZH-FPV
成功完成的结果不能互相替代。

四种模式完整结果保存在 `install/openvins-backend-f32/board-results/results-uzh/`。
复现完整 FP32 与四种模式统一比较：

```bash
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp32
python3 scripts/vio-compare/probes/backend-f32/prepare_configs.py \
  install/openvins-backend-f32/host-uzh/config-uzh-original \
  install/openvins-backend-f32/board-config \
  --name uzh --variants fp32 fp32packed \
  --runtime-root /mnt/data/vio-compare/backend-f32
# 部署对应程序/配置，在板上执行上节的 'fp32 fp32packed' 命令；取回结果后：
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/board-results/results-uzh \
  --variants original mixed fp32 fp32packed \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --shift-ns -16684572
```

## 当前建议

同段 A53 的初始化后视觉处理 perf 结果见 [PERF.md](PERF.md)：LK 主迭代 25.00%，
Scharr 7.55%，RANSAC/基础矩阵相关函数 8.84%（其中 FP64 SVD 7.21%），
去畸变捕获调用链约 0.47%。样本排除了 PNG 解码和额外协方差诊断。

完整 FP32 在 A53 UZH-FPV 上成功完成且有性能收益，但自录强运动录像测试出现实际数值故障，
因此只保留为默认关闭的实验。
优先使用协方差仍为 FP64 的局部优化，并继续检验强运动数据的轨迹正确性。
下一步若要扩大 FP32 范围，应该把协方差存储、残差协方差分解及写回保留 FP64，
分别试 Jacobian/矩阵乘法，不应删除负值检查来换取“跑完”。

原始日志、逐帧轨迹、stage CSV、退出码及 `comparison.json` 保存于
`install/openvins-backend-f32/board-results/results-{mild,strong}/` 和
`install/openvins-backend-f32/host-uzh/results-uzh/`。

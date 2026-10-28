# C906＋TPU LK 卸载原型

已在 TideSense 上运行完整的金字塔 LK 迭代闭环。A53 一次提交输入后，C906
独立推进全部层级和迭代；Linux 邮箱等待会休眠，不参与逐轮提交或轮询 TPU。
本实验保持 OpenVINS 的现有运行程序及后端精度配置。

当前是独立的算法与并行调度实验，尚未接入 OpenVINS 实时前后端流水线。
重点是释放 A53 给后端使用；C906 的单次延迟高于 CPU LK 也可能有价值，
但持续运行时仍需满足双目完整工作量的吞吐和队列延迟要求。

## 计算分工

| 处理器 | 工作 |
| --- | --- |
| A53 | 构建原始 U8 金字塔、准备特征点和初值、提交一次共享缓冲区 |
| C906 | 更新采样 ROI 和插值权重、提交并等待 TPU、FP32 解 2×2 方程、判断收敛、推进层级 |
| TPU | U8→BF16、窗口 Scharr、双线性采样、Hessian、残差、梯度乘积和窗口求和 |

Scharr 仅计算特征窗口的邻域，避免在 A53 上计算和转换整幅梯度图。
模板及梯度在一批点的迭代期间留在 TPU 本地内存，C906 每轮只读回每个点的
两个归约值。每次 Linux 调用覆盖全部点和金字塔层级，A53 不会被逐轮唤醒。

窗口为 16×15，最多 5 层、30 次迭代，epsilon 为 0.01。默认每批 16 点，
可编译为 8/32 点；32 点对容易样本稍快，16 点在测试的困难样本中减轻了
等待一批中最慢点的问题。已收敛点不再更新描述符。内核仍按整批计算，尚未
实现活跃点压缩。Scharr 使用 3×3 深度卷积和边界掩码，保持 OpenCV 的
REFLECT_101 灰度边界和零填充梯度边界。

## A53 实测

2026-10-28，A53 1 GHz，OpenCV 3.4.20 单线程，UZH-FPV
`indoor_forward_6` 的 16 对连续图像：第 200、450、700、950、1200、1450、
1700、1940 帧，两颗相机，原始 640×480、约 30 Hz 间隔，每对 150 个相同
FAST 点。没有额外直方图均衡。每项预热后测三次，金字塔测试交替安排两条
路径的顺序，保留点级输出；这不是双目完整前端或整段 VIO 的耗时。

| 单目一对图像的项目 | 平均耗时 |
| --- | ---: |
| A53 原有金字塔及全图梯度＋OpenCV LK | 约 23.9 ms |
| 卸载路径 A53 准备：无梯度金字塔＋U8 打包＋缓存清理 | 约 8.7 ms |
| C906＋TPU 完整 LK 及邮箱往返 | 约 10.4 ms |
| 卸载路径串行相加 | 约 19.1 ms |

串行延迟平均降低约 20%，但更主要的收益是 A53 的图像准备完成后，可以在
等待期间处理上一帧后端，而不是继续执行约 10–15 ms 的 CPU LK。描述符在
缓冲区初始化时生成，测试单列 `compile_ms`，不算进每帧路径；离线 FP32
标量参考程序仅用于检查数学逻辑，不作为性能基准。

## 并行验证

`C906_LK_OVERLAP=1` 启用代表性的 **FP64 协方差更新负载**，维度为
160 个状态、32 个观测，包括矩阵乘法、LLT 求解、协方差更新；重复次数在
板上标定到约 30–35 ms。它不是完整 OpenVINS 后端，不包含真实状态管理和
其他后端步骤。

困难样本第 1450 帧、右相机的结果：

| 项目 | 实测 |
| --- | ---: |
| FP64 矩阵负载单独运行 | 34.7 ms |
| LK 单独运行、含邮箱往返 | 16.3 ms |
| 两项串行相加 | 51.0 ms |
| 并行完成全部工作 | 36.3–36.7 ms |
| A53 邮箱等待线程实际 CPU 时间 | 0.070–0.073 ms |

三个并行试验中，FP64 矩阵结果与单独运行完全一致，LK 有效标记和坐标也一致
（`checksum_error=0 lk_delta_max_px=0`）。线程启动、调度和内存争用仍有开销，
实测不是严格的 `max(后端, LK)`。后续真实流水线应在后端处理第 N 帧时，由
C906/TPU 跟踪第 N+1 帧，并向后端交付不可变的特征快照，不能并发改写同一个
OpenVINS FeatureDatabase。

## 精度边界

16 对图像共 2400 个点，有效/失败判定与 OpenCV **2400/2400 一致**，两者
共同有效 2359 点。坐标差中位数约 0.0016 px，P95 约 0.007 px，P99 约
0.018 px。不过仍有 **3 个点超过 0.1 px，最大约 1.79 px**；它不是逐位
等价替代。TPU 使用 BF16，C906 坐标及方程求解使用 FP32。这些是视觉前端
的实验数值格式，没有将 VIO 后端改为 FP32。

最大差异点的 FP32 标量参考同样与 OpenCV 相差约 1.78 px，TPU 与该参考只差
约 0.006 px，因此不能将这一个点的差异直接归因于 BF16。另一个点中，
FP32 参考接近 OpenCV，TPU 相差约 0.41 px，仍需检查数值精度及收敛路径。

还需要完整 VIO 回放验证轨迹及困难点处理，不能用本次有效点数或中位误差
推断激烈运动时一定不发散。当前保持实验功能默认关闭。

原始日志与点级 CSV：`install/c906-reload/lk-final-results/`；并行日志：
`install/c906-reload/lk-overlap-final.log`。早期整图 BF16 转换版本的记录在
`install/c906-reload/bf16-prototype/` 及 `lk-results/`，不应混用其耗时。

## 编译和部署

```sh
scripts/c906/build.sh --lk-probe
# 可选：C906_LK_BATCH=8 或 32，默认 16，必须将匹配的主机程序与固件一起部署。
```

除原有 SDK 依赖外，需要已有静态 OpenCV stage、Eigen 头文件，以及
`install/openvins-backend-f32/a53-fp32/build/CMakeFiles/run_fisheye.dir/link.txt`。
构建只复用这个文件的依赖库清单，移除 OpenVINS 库，不使用该构建的 FP32
编译选项或估计器代码。`TIDESENSE_C906_TPU_LK` 默认 OFF，由本命令开启。

输出在 `install/c906-reload/`：

- `cvirtos-reload-tpu-lk.bin`：含 LK 及在线重载钩子的固件。
- `c906-tpu-lk`：A53 静态测试程序。
- `c906_tpu_bridge.ko`：独占 TPU 的时钟、复位和邮箱桥接模块。

复制到设备 `/mnt/data/c906-reload/`，停止摄像头、VIO 和推理程序后执行：

```sh
cd /mnt/data/c906-reload
[ -e /dev/c906-loader ] || /sbin/insmod ./c906_loader.ko
./c906-reload load ./cvirtos-reload-tpu-lk.bin
/sbin/rmmod cv181x_tpu
/sbin/insmod ./c906_tpu_bridge.ko
./c906-tpu-lk synthetic 150 3 synthetic.csv
./c906-tpu-lk prev.pgm next.pgm 150 3 points.csv
C906_LK_OVERLAP=1 ./c906-tpu-lk prev.pgm next.pgm 150 3 overlap.csv
```

输入图像最大为 640×480，每次最多 160 点，使用 2 MiB 连续共享 ION 缓冲区。
Linux TPU 驱动与实验不能同时持有 TPU。输入、描述符和输出缓冲区在调用完成
前都归 C906/TPU 使用；将来接流水线时应持有或双缓冲这些内存。

本机可复现 16 对图像的上传、运行和汇总。设备需已经加载上述固件及桥接模块：

```sh
SSHPASS=cvitek python3 scripts/vio-compare/probes/c906-tpu/benchmark_lk.py \
  --dataset install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay \
  --output install/c906-reload/lk-final-results
```

支持 SSH 密钥；使用 `SSHPASS` 时需要 `sshpass`。有独立 known_hosts 文件时，
再传 `--known-hosts /path/to/known_hosts`。完整输出包括点级 CSV、原始日志、
`summary.json`、`aggregate.json` 和 `outliers.json`。

结束实验后恢复：

```sh
/sbin/rmmod c906_tpu_bridge
/sbin/insmod /mnt/system/ko/cv181x_tpu.ko
./c906-reload load ./cvirtos-reload-base.bin
```

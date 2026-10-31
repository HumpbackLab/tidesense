# C906＋TPU LK 卸载原型

已在 TideSense 上运行完整的金字塔 LK 迭代闭环。A53 一次提交输入后，C906
独立推进全部层级和迭代；Linux 邮箱等待会休眠，不参与逐轮提交或轮询 TPU。
本实验保持 OpenVINS 的现有运行程序及后端精度配置。

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

本页为独立原型。OpenVINS 集成入口见 [LK 卸载](../../openvins/README-tpu.md)。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

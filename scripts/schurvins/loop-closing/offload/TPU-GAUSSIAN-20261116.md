# TPU 7×7 Gaussian 卸载实验（2026-11-16）

分支 `feat/schurvins-loop-closing`，基线提交 `dd9c00d` 保存此前回环检测、IVE-ST=3、ORB 复用及调度实验。
本轮在此之后实现可选的 **Linux TPU 高斯滤波**，C906 未参与计算；IVE 继续产生 ST 角点响应。
按后续用户要求，已编入 TPU Gaussian 时，普通入口和 `run-ive-tuned.sh` 默认使用 `tpu-q9`；
`SCHUR_ORB_BLUR=cpu` 保留原浮点回退路径。下面的测量数据来自切换默认值之前，使用显式模式作对照。

## 实现与精度

ORB 在子矩形 ROI 上调用 7×7、sigma=2、非 isolated 的 `GaussianBlur`。
本 SDK OpenCV 3.4.20 在这条路径使用浮点可分离滤波，不能直接用原 OpenVINS 5×5 下采样核替换。
实验使用两次七点整数滤波，Q9 系数为 `[36,67,98,110,98,67,36]`，和为 512；
水平输出先舍入为 U8，再做垂直滤波，所以 **它是近似实现，不是位级等价替换**。

- 复制原父图的 3 像素 halo，保留真实 ROI 外围像素，未把每层视作独立边界。
- 图像分为 8 个横向通道和最多 64 行的小块，水平中间结果留在 TPU 本地 SRAM。
- 同一次 ORB 调用的所有金字塔层生成一个程序，只提交一次 TPU 任务。
- 按层尺寸缓存程序与 ION 缓冲；正常检测 / 已有点描述分别复用 8 层和 1 层布局。
- 包含输入拷贝、cache clean、TPU 同步执行、invalidate 和回拷；输出带 DMA 越界哨兵。

当前限 mono8、1..8 层、单层最大 640×480。640×400 的 8 层与 1 层缓存显式 ION 缓冲合计约 2.07 MiB，
另外还有 SDK 命令缓冲，不能仅凭进程 RSS 判断全部媒体内存占用。

28 组常值、渐变、棋盘格、随机图、1×1 / 64×64 / 533×333 / 640×400 尺寸测试，
TPU 与 CPU 标量 Q9 模型逐像素一致，全部哨兵通过。
新增的 A53 NEON Q9 对照也在相同 28 组中与标量模型完全一致，和 TPU 使用相同系数及舍入规则。
在这些测试及两个自然图样本上，与原 OpenCV 的最大像素差为 1；这不是所有输入的误差保证。

固定图像诊断中点的位置、响应、角度、尺度、octave 及点数不变，描述子发生变化：

| 图像 / 点类型 | 描述子数 | 有变化的描述子 | 每个 256-bit 描述子平均变化 | 单行最大变化 |
| --- | ---: | ---: | ---: | ---: |
| 本板录制左图 / 新检测点 | 346 | 280 | 2.165 bit | 12 bit |
| 本板录制左图 / 120 个已有点 | 120 | 92 | 2.142 bit | 9 bit |
| 旧录制纹理图 / 新检测点 | 469 | 414 | 2.388 bit | 9 bit |
| 旧录制纹理图 / 120 个已有点 | 120 | 87 | 1.217 bit | 4 bit |

两个自然图的像素差异比例约 9.0% / 9.6%，非零差异均为 1。
小灰度误差仍会改变二进制比较结果，不能仅凭像素差小推断词袋或回环检出率相同。
当前仍没有移动闭环精度验证；切换默认值不改变上述近似误差结论。
同图 CPU 回退的点数与描述子 checksum 已与原 OpenCV 库核对一致。

## 无文件读图的内存微基准

图像只在计时前加载，8 层金字塔也在计时前构造。输入恢复拷贝在两条路径的计时之外，
TPU 所需的打包、缓存同步、提交、等待及输出拷贝全部计入 TPU 耗时。
先预热程序和可复用 scratch，再交替执行浮点 CPU / NEON Q9 / TPU 100 轮，关闭 CPU 参考验证。

| 每次 8 层滤波 | CPU 浮点原版 | A53 NEON Q9 | TPU Q9 |
| --- | ---: | ---: | ---: |
| 完整调用墙钟时间 | 19.488 ms | 10.593 ms | 3.606 ms |
| 调用线程 CPU 时间 | 19.449 ms | 10.552 ms | 2.436 ms |

与原浮点实现相比，墙钟耗时减少 81.5%，调用线程 CPU 时间减少 87.5%，其中一部分收益来自数值实现变化。
**与相同 Q9 算法的 NEON 实现相比，TPU 墙钟耗时仍减少约 66.0%，调用线程 CPU 时间减少约 76.9%。**
含一次预热的 101 次调用统计中，提交等待约 127 ms、打包约 122 ms、回读约 111 ms，剩余开销已主要转为搬运与同步。
CPU Q9 是探针中的同精度对照，当前主程序运行时选项仍只有 cpu（原浮点）和 tpu-q9。

原有 ORB 探针每轮两次 detect+compute，加一次 120 点 compute；固定图重复 20 轮：

| ORB 组合任务 | CPU Gaussian | TPU Q9 |
| --- | ---: | ---: |
| 总耗时 | 2465.75 ms | 1738.02 ms |
| Gaussian 阶段累计 | 898.43 ms | 171.91 ms |

该组合耗时减少 29.5%。探针的两次检测也检查同一实现的重复输出是否稳定。
此组合对照包含了数值实现变化，不能把 29.5% 全部归因于 TPU 硬件。
实际已启用 ORB 复用的回环工作流不再重复检测，不能把此数字直接换算为 VIO 帧率。

## 实时 VIO

同一二进制，顺序 CPU → TPU → TPU → CPU，每轮 600 帧。
均为实时双目 / IMU，IVE=3、CPU NEON 匹配、ORB 复用与 3D 预检查开启、回环 nice=10，关闭 ROS 与预测。
FPS 剔除前 50 帧，按已处理图像时间戳跨度计算；没有离线读图。

| 轮次 | VIO FPS | 回环任务 | Gaussian TPU 提交 | 进程 CPU 采样 | 相机待处理帧替换 |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPU 1 | 19.9548 | 13 | 0 | 52.88% | 0 |
| TPU 1 | 19.9545 | 10 | 19 | 40.56% | 0 |
| TPU 2 | 19.9543 | 14 | 26 | 45.23% | 0 |
| CPU 2 | 19.9542 | 10 | 0 | 45.02% | 0 |

全部 tracking=600、imu_fused=600、退出码 0，已经达到约 20 Hz 的相机输入上限。
未测到 VIO FPS 提升。各次关键帧与其他工作量不同，不将进程 CPU 百分比差异全归因于 Gaussian 卸载。
四轮均未进入几何验证，也未确认回环，不能评价繁忙几何场景或移动回环质量。
性能轮 `verified_calls=0`，日志中像素误差字段为 0 表示未计算，实际精度以独立诊断为准。
Gaussian 的实际 TPU 调用看 `gaussian_offload.submissions`；
`loop_offload.tpu_calls` 仅统计描述子匹配，本轮该匹配仍在 A53，因而为 0。

## 构建与运行

```sh
SCHUR_ROS_BACKEND=none SCHUR_WITH_LOOP_CLOSING=ON \
  SCHUR_LOOP_OFFLOAD=ON SCHUR_LOOP_IVE_ORB=ON SCHUR_LOOP_TPU_GAUSSIAN=ON \
  scripts/schurvins/build.sh
```

IVE 构建默认将该编译开关设为 ON，要求 IVE 构建开关 ON 以使用固定版本 ORB 钩子；运行时检测器仍可选 FAST 或 IVE。
显式 `SCHUR_LOOP_TPU_GAUSSIAN=OFF` 可构建 CPU 滤波版本。
输出使用独立 `*-tpu-gaussian` 目录。需板上 Linux `cv181x_tpu` 驱动正常绑定；无需更换小核固件。
设备实验目录为 `/mnt/data/schur-tpu-gaussian-20261116`：

```sh
cd /mnt/data/schur-tpu-gaussian-20261116
./run-tpu-gaussian.sh --vio schurvins.yaml auto /tmp/tpu-poses.csv 600
# 同一程序使用原 CPU 滤波：
SCHUR_ORB_BLUR=cpu ./run-tpu-gaussian.sh --vio schurvins.yaml auto /tmp/cpu-poses.csv 600
```

`SCHUR_ORB_BLUR=cpu|tpu-q9`，已编入该模块时默认 tpu-q9；不含该模块的构建使用 CPU。
`SCHUR_ORB_BLUR_VERIFY=1` 额外运行 CPU Gaussian 并记录像素差，增加 CPU 开销，应与性能轮分开。
ORB 探针的 `SCHUR_ORB_COMPARE_CPU=1` 另比较点集及描述子位差，仅用于诊断。

## 默认路径切换验证

删除 CMake 缓存中的 `SCHUR_LOOP_TPU_GAUSSIAN` 后重新配置，IVE 构建自动取 ON。
更新设备实验目录的主程序与 ORB 探针后，未设置 `SCHUR_ORB_BLUR` 的固定图测试实际提交 TPU 3 次，
描述子 checksum 与显式 `tpu-q9` 一致；显式 `cpu` 无 Gaussian TPU 提交，checksum 与此前 CPU 基线一致。

通过普通 `run-ive-tuned.sh` 入口、不设置滤波环境变量实时运行 60 帧：
`tracking=60`、`imu_fused=60`，Gaussian TPU 提交 2 次，正常退出。
这次短程启动检查的整体处理帧率为 19.26 FPS，覆盖初始化开销；不是新的稳态性能对照。
设备仅更新 `/mnt/data/schur-tpu-gaussian-20261116` 中的实验程序，原生产程序及标定未变。

CMake 探针目标为 `schur_gaussian_probe` 和 `schur_orb_profile`：

```sh
./bin/gaussian_probe                  # 28 组硬件正确性测试
./bin/gaussian_probe IMAGE.pgm 100    # 内存输入的 8 层 Gaussian 对照
SCHUR_ORB_DETECTOR=ive-st SCHUR_ORB_BLUR=tpu-q9 \
  SCHUR_ORB_BLUR_VERIFY=1 SCHUR_ORB_COMPARE_CPU=1 ./bin/orb_profile IMAGE.pgm 1
```

主机 `install/schur-tpu-gaussian-20261116/` 保留 `results/`、`summary.json`、
`micro-summary.json`、构建日志、二进制、`BUILD-INFO.json` 和 `device-final.txt`。
原生产二进制、标定和固件未修改；测试后无遗留 VIO 进程。

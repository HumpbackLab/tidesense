# SchurVINS 回环检测卸载实验

这是可选回环实现。普通入口默认使用原 FAST 检测和 CPU 匹配；编入 TPU Gaussian 的构建默认使用 TPU Q9 预滤波。
不运行位姿图优化，不修改 VIO 状态。实测见 [VALIDATION.md](VALIDATION.md)。
不含读图的实时测试见 [LIVE-20261116.md](LIVE-20261116.md)。
阈值从 16 校准为 3 的过程和实时对照见 [IVE-TUNING-20261116.md](IVE-TUNING-20261116.md)。
ORB 复用、3D 数量预检查及后台线程调度见 [ACCELERATION-20261116.md](ACCELERATION-20261116.md)。
可选 TPU 7×7 Gaussian 的近似误差与收益见 [TPU-GAUSSIAN-20261116.md](TPU-GAUSSIAN-20261116.md)。

## 构建

```sh
SCHUR_ROS_BACKEND=none SCHUR_WITH_LOOP_CLOSING=ON \
  SCHUR_LOOP_OFFLOAD=ON SCHUR_LOOP_IVE_ORB=ON \
  scripts/schurvins/build.sh
C906_OUT="$PWD/install/schur-loop-offload/c906" scripts/c906/build.sh --tpu-probe
```

IVE 开关生成固定 OpenCV 3.4.20 `orb.cpp` 的带剖析副本，不改动依赖源码。
关闭 IVE 运行开关时，描述子已与原库逐字节比较。
`SCHUR_LOOP_IVE_ORB` 默认 OFF，要求 `SCHUR_LOOP_OFFLOAD=ON`。
输出使用独立 `*-loop-offload-ive` 目录。
IVE 构建现在默认同时编入 TPU Gaussian，输出再追加 `-tpu-gaussian`；
`SCHUR_LOOP_TPU_GAUSSIAN=OFF` 可构建不含该模块的 CPU 滤波版本。
已编入时不设置 `SCHUR_ORB_BLUR` 即使用 `tpu-q9`，显式 `SCHUR_ORB_BLUR=cpu` 切回原浮点路径。

单独配置已有构建目录时，先运行 `profile_orb.py`，再设置
`SCHUR_LOOP_OFFLOAD=ON`、`SCHUR_LOOP_IVE_ORB=ON`。
`SCHUR_LOOP_ORB_PROFILE=ON` 另提供 `schur_orb_profile` / `schur_orb_reference` 探针。
匹配测试目标为 `schur_loop_match_probe` 和 `schur_loop_matcher_test`。

## IVE 角点变体

```sh
SCHUR_LOOP_CLOSING=1 SCHUR_ORB_DETECTOR=ive-st \
  SCHUR_LOOP_MATCH_BACKEND=cpu-batch ./run.sh --replay /path/to/record /tmp/poses.csv
```

IVE `STCandiCorner` 计算 Shi–Tomasi 响应，A53 做局部极大值筛选；
继续使用原 OpenCV ORB 的边界过滤、分层配额、Harris 排序、方向和 BRIEF。
**它替换 FAST 检测器，会改变点集及描述子，不是原 ORB 的等价硬件实现。**
词袋没有重新训练，检出率、误检率和移动场景的影响尚未验证。
`SCHUR_ORB_DETECTOR=fast` 或不设置时使用原 FAST。
`SCHUR_ORB_IVE_QUALITY` 默认 3，范围 1..255，不是 FAST 阈值；值越小通常保留更多角点。
此前实验采用 16。本次按同图 FAST 点数扫描后改为 3，场景改变时仍需重新核对。
`run-ive-tuned.sh` 安装到实验包根目录，可直接开启 IVE、阈值 3 和 CPU NEON 匹配：

```sh
./run-ive-tuned.sh --vio schurvins.yaml auto /tmp/poses.csv 600
```

该入口还默认开启以下实验选项，均可通过同名环境变量覆盖：

| 变量 | 此入口默认 | 行为 |
| --- | --- | --- |
| `SCHUR_LOOP_REUSE_ORB` | `1` | 复用同一快照第一次 ORB 提取，避免重复检测和描述 |
| `SCHUR_LOOP_PRECHECK_3D` | `1` | 3D 匹配总数已不足最终门槛时，提前拒绝候选 |
| `SCHUR_LOOP_NICE` | `10` | 仅降低回环工作线程的 CPU 调度优先级 |

普通 `run.sh` 的两个计算选项默认关闭，工作线程继承优先级。
布尔值仅接受 0/1，nice 显式设置接受 0..19；0 可作同优先级对照。
低优先级让 VIO 优先处理输入，可能增加回环等待时间和丢弃输入数量。
上述选项保留 IVE 阈值、RANSAC 置信度/阈值/迭代上限及最终 3D 接受门槛。

`ORB_POINTS` 按金字塔层报告原始、边界过滤、初筛及最终点数。
可将同图扫描日志命名为 `left-N-fast.log` / `left-N-q3.log` 等，离线选择全局阈值：

```sh
python3 scripts/schurvins/loop-closing/offload/tune_points.py LOG_DIR --output selection.json
```

选择依据为最终点数相对误差；同时报告层间分布和候选点数差异。
运行时使用固定阈值，不额外执行 FAST。

此路径使用 Linux `cv181x_ive` 驱动，不需要 C906 固件，也不能让 C906 同时接管 IVE。
支持 U8 金字塔层 32..640 × 32..480；按 16×4 对齐并复制边缘。
输入、输出和 scratch 共约 2.59 MiB cached ION，输出与 scratch 带 DMA 越界哨兵。
CPU 做金字塔、图像拷贝、非极大值筛选和 ORB 后续步骤。
退出日志 `ive_orb` 给出实际硬件调用数；`ORB_PROFILE` 给出各阶段累计耗时。

## 描述子匹配

`SCHUR_LOOP_MATCH_BACKEND` 可选：

| 值 | 距离计算 | 最佳匹配归约 |
| --- | --- | --- |
| `cpu`（默认） | 原逐对 `cv::norm` | 原 CPU 实现 |
| `cpu-batch` | A53 NEON XOR/popcount | A53 |
| `linux-tpu` | Linux TPU | A53 |
| `c906` | C906 标量 popcount | C906 |
| `c906-tpu` | C906 提交 TPU | C906 |

按词袋节点分组，仅比较相同节点的描述子；保留原来的双向顺序、严格距离阈值和相等距离的选择顺序。
TPU 每批最多 1024 对：U8 XOR → popcount LUT → BF16 求和；0..256 整数距离可精确表示。
每个匹配任务最多 1024 个查询点，超出会报错。
共享缓冲区 1 MiB。TPU 提交以批为单位同步等待，并未实现 CPU/TPU 重叠。

`SCHUR_LOOP_MATCH_VERIFY=1` 额外运行原 CPU 参考并比较全部匹配索引；
它只证明**匹配**等价，不能证明 IVE 检测器等价。验证额外时间应与性能测量分开。
退出日志 `loop_offload` 给出 `verified_calls`、`pairs`、`tpu_calls`、提交和设备耗时。

## C906 与 TPU 所有权

必须先有支持在线重载的开机固件，详见 [C906 说明](../../../c906/README.md)。
旧固件 `reload=0` 需要先备份本板 FIP、仅替换 C906 段并重启一次；本目录不会自动写 FIP。
后续实验固件可在 RAM 中热重载，整板重启后恢复 BOOT 中的固件。

停止相机、推理及其他邮箱用户后，在板端执行：

```sh
cd /mnt/data/schur-loop-offload/c906
[ -e /dev/c906-loader ] || /sbin/insmod ./c906_loader.ko
./c906-reload status                    # 要求 echo=0 reload=1
./c906-reload load ./cvirtos-reload-tpu.bin
/sbin/rmmod cv181x_tpu
/sbin/insmod ./c906_tpu_bridge.ko        # 不设置 ive=1，保留 Linux IVE
```

之后才可运行 `SCHUR_LOOP_MATCH_BACKEND=c906-tpu`。TPU 归 C906 独占。
`linux-tpu` 则要求 Linux TPU 驱动保持绑定，不加载 C906 桥接。
停止所有实验进程后，恢复 Linux TPU 和兼容普通小核固件：

```sh
/sbin/rmmod c906_tpu_bridge
/sbin/insmod /mnt/system/ko/cv181x_tpu.ko
./c906-reload load ./cvirtos-reload-base.bin
```

若邮箱超时或重载失败，按 C906 文档重启恢复，不能在任务仍执行时强行换驱动。
IVE 单独运行和 `cpu-batch` 匹配都不要求替换固件或 TPU 驱动。

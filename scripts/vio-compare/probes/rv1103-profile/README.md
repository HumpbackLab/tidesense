# RV1103 单目 OpenVINS 回放与 perf 内核诊断（2026-10-25）

当前完整诊断镜像已成功启动，已在 `172.32.0.93` 完成两轮无 perf 的 400 帧
回放和一轮硬件 cycles 采样，见 [最终耗时及热点报告](RESULTS.md)。
后续已接入 RKIVE 金字塔和 LK，完成各模式回放与精度对比，见 [RKIVE 加速试验](RKIVE.md)。
VIO 本体的进一步简化与 20 Hz 计算预算见 [推荐配置及长尾结果](LITE.md)：
推荐两轮均值约 34 ms、P95 44 ms，仍有少数单帧超 50 ms。
后端对比见 [OpenVINS ARM32 优化](BACKEND.md)、[ov-SchurVINS](SCHUR.md) 和
[Schur FP32 数值验证](SCHUR-FP32.md)。完整 Schur FP32 未通过回放；混合精度可完成，
但该片段上仅有少量性能收益且 ATE 增加，实验脚本默认保留 FP64。
下面保留最初只换内核引入模块 ABI 不兼容的分析与旧内核基线；未取得该次故障的
串口日志，具体崩溃位置仍不能当作已观测事实。

## 内核故障分析

通过 SSH 保存完整 32 MiB boot 分区，再将新 FIT 镜像用
`dd if=boot-perf.img of=/dev/block/by-name/boot bs=1M conv=fsync` 写入并重启。
写入后按镜像长度读取分区，其 SHA256 与上传镜像相同。

- 完整旧 boot 分区 SHA256：`09acca86682696f364cdfb9025e0230e992bbdced462d06c58d4345fd313c09f`。
- 新 boot 镜像 SHA256：`54e3f81a8d86654232c3e41803bf08584912ddfabc595a39db3cc7c72c3ec721`。
- 旧分区前缀与 SDK 原 boot.img 逐字节一致。
- 新旧 FIT 的 FDT SHA256 都是 `cd992c39ff29d3b75f78cf417fe2eec3f3ab893a274ebae14350b2b7d5707a98`。
- 保留原 resource.img、根文件系统和 OEM 驱动。

错误在于只替换内核，没有同步重建模块。SDK `init/Kconfig:2401` 中：

```kconfig
config MODULES_TREE_LOOKUP
    def_bool y
    depends on PERF_EVENTS || TRACING || CFI_CLANG
```

开启 PERF_EVENTS 自动打开 MODULES_TREE_LOOKUP，改变内核模块结构布局。
从新旧 `kernel/module.o` 的 DWARF 信息直接提取：

| 项目 | 旧内核 | 新 perf 内核 |
| --- | ---: | ---: |
| sizeof(struct module) | 384 | 512 |
| sizeof(struct module_layout) | 20 | 48 |
| module.core_layout 偏移 | 216 | 256 |
| module.arch 偏移 | 256 | 352 |
| module.kallsyms 偏移 | 312 | 408 |
| sizeof(struct task_struct) | 1072 | 1224 |

现有 `rk_dvbm.ko`、`mis5001.ko`、`video_rkcif.ko`、`rockit.ko`、
`mpp_vcodec.ko`、`rknpu.ko` 的 `.gnu.linkonce.this_module` 均为 **384 字节**。
用新内核编译的最小探针 `module_layout_probe.mod.o` 中该段为 **512 字节**。
二者 vermagic 却都是 `5.10.160 mod_unload ARMv7 thumb2 p2v8`。

该内核没有启用 MODVERSIONS，`kernel/module.c:1397` 的
`check_modstruct_version()` 直接返回 1；模块加载器也没有校验 this_module
段长度等于新结构体长度。因此相同版本字符串不能阻止这种结构体错配。
新内核读写旧模块中错误偏移，甚至访问旧结构体边界之外，具有实际内存破坏风险。

启动顺序为 `S21appinit → RkLunch.sh → insmod_ko.sh`，第一个模块是
`rk_dvbm.ko`，此后才启动 S50 的 SSH/USB 服务。这与网络完全没有恢复的现象
一致，但最终需要串口 Oops/启动日志确认。不能仅凭 SSH 不可达区分模块异常、
内核更早期异常或其他启动故障。

修复方向是使用同一配置重新构建内核和全部启动所用模块，包括 OEM 外部模块，
并核对其他因 PERF_EVENTS 改变的内核结构体。只重新打包 FIT 不能消除 ABI 断裂。
构建脚本已补充模块布局检查，存在错配时返回失败。

## 已完成的回放

RV1103 Cortex-A7 单核，固定 1.104 GHz，Linux 5.10.160 原内核，64 MiB 物理内存，
24 MiB 摄像头 CMA，Linux MemTotal 33636 KiB，无 swap。静态 ARM hard-float
OpenVINS FP64，OpenCV/OMP 单线程；EuRoC V1_01 的 25 秒片段，单目 752×480、
200 个目标特征、11 clones、max_slam=0，FEJ 与相机在线内外参/时间偏移标定开启。
原生初始化，5 帧预热后统计 400 帧；总处理 495 帧，记录 405 个已初始化状态。

源码复制自 `/home/ncer/fpga_openvins` 的干净上游快照与回放适配器，上游基准提交
`93adc241390d13e99232652cf05cbe18a93c7bea`，具体差异见复制源码的
`LOCAL_CHANGES.md`。沿用关闭可视化活跃点重三角化的配置，保留外点检验。
本次另加计时，未改动算法门限。不是 OV9281 实时采集或硬件加速测试。

| 阶段 | 平均 ms/帧 |
| --- | ---: |
| 图像读取及 PGM 解码（VIO 之外） | 22.562 |
| 前端总计 | 67.901 |
| IMU 传播 | 3.084 |
| MSCKF 更新 | 76.986 |
| 边缘化/清理 | 0.991 |
| 后端总计 | 81.061 |
| 适配层及其他开销 | 1.024 |
| 完整 VIO | 150.001 |
| VIO + 图像读取解码 | 172.564 |

完整 VIO P95 为 294.805 ms。平均耗时折算约 6.67 fps，计入读取解码约
5.79 fps；这是离线吞吐估算。回放轨迹 SE(3) 对齐、不拟合尺度，位置 ATE RMSE
0.046281 m，最大误差 0.096252 m，覆盖 20.2 s。

细分计时、CPU、峰值内存和原始轨迹保存在
`install/rv1103-openvins-20261025/results/`。`analyze.py` 只统计 measured=1 的帧。
匹配总时间包含 LK/归一化/RANSAC，汇总时不要与这些子阶段重复相加。
各阶段 P95 不能直接相加。

## 产物和复现

- 本机工作目录：`install/rv1103-openvins-20261025/`。
- 板端工作目录：`/root/openvins-profile-20261025/`。
- 原 boot 分区备份：上述两处的 `kernel/boot-original-partition.img`（本机）和
  `boot-original-partition.img`（板端）。
- `build.sh`：隔离构建回放、内核、静态 perf；最后检查 OEM 模块 ABI。
- `prepare-dataset.py`：无损 PNG→PGM，供未启用 PNG 的静态 OpenCV 使用。
- `run-board.sh baseline NAME`：无 perf 的阶段计时。
- `run-board.sh perf NAME`：仅在内核和模块兼容且 PMU 已验证后使用。
- `audit-module-abi.py OLD_MODULE_O NEW_MODULE_O OEM_KO_DIR`：提取并检查布局。

perf 可执行文件版本为 5.10.160，已部署 `/usr/bin/perf`。
后续完整镜像启动后已验证 ARM PMU 计数和采样，结果见 [RESULTS.md](RESULTS.md)。

## 后续生成的完整诊断固件

已经生成 `install/rv1103-perf-image-20261025/images/update.img`，是 SDK 的
Rockchip 固件整包，配置为 RV1103_Luckfox_Pico、SD_CARD、Buildroot。
不是可以直接当作整卡裸镜像写入 SD 卡的文件。

这份固件用于离线回放：包含 perf 内核、静态 perf、OpenVINS FP64、EuRoC
25 秒单目数据、计时与采样脚本。33 个有源码的内核模块已在相同配置下重编，
实际从 OEM 镜像提取后检查，this_module 段全部为 512 字节。
SDK 提供的预编译厂商模块没有混入镜像。

摄像头/媒体及 Wi-Fi 模块自动加载被关闭，保留 RNDIS 与 SSH；它不是保留
摄像头采集功能的产品镜像。必须整包匹配使用，单独替换 boot 仍会与设备原有
OEM 驱动冲突。用户已烧写并启动这份完整镜像；随后完成了 PMU 验证和三轮回放，
没有再次替换内核或更改镜像。

在电脑上已完成：三个 ext4 文件系统检查、关键文件 root:root 所有权与执行
权限检查、旧模块排除检查，以及 update.img 解包后的七个分区和 loader
与打包输入逐一 SHA256 比对。校验和与详细结果位于镜像目录的 `SHA256SUMS`
以及工作目录的 `package-validation.json`、`packed-module-abi-audit.json`。
源码构建与镜像打包脚本为 `build-image.sh` 和 `mkfs-images.sh`。

上板后通过 RNDIS 的 `172.32.0.93` 登录（root/luckfox），运行：

```sh
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
sh /root/openvins-profile-20261025/run-board.sh baseline baseline
perf stat -e cycles,instructions -- sleep 1
sh /root/openvins-profile-20261025/run-board.sh perf perf
```

先验证启动和 `perf stat`，再执行采样回放。程序不会开机自动运行。

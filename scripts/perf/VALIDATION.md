# perf 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README 清理前保留的上板与调频记录

### 构建背景

构建脚本使用 SDK ARM64 工具链及 `linux_5.10/tools/perf`，下载并校验
elfutils 0.184（校验值来自 SDK Buildroot），使用 SDK 的 ARM64 zlib。
主机需要 make、gcc、flex、bison、curl、tar、bzip2、gettext 等构建工具。
产物放在 `install/soc_sg2002_tidesense_arm64_sd/perf/`，同时将 perf
安装到该板 rootfs 的 `/usr/bin/perf`，后续打包根文件系统时会包含它。

内核启用 `PERF_EVENTS`、`HW_PERF_EVENTS`、`ARM_PMU`（默认选中）、
`KALLSYMS_ALL`、`STACKTRACE`，保留原有 `FRAME_POINTER`。
板级设备树将 PMU 中断列表修正为单个 A53 对应的 GIC SPI 119。
SDK 默认节点有两个中断、仅一个 affinity 条目，会造成 ARM PMU 注册失败。
内核配置变化后必须同时部署重新编译的内核模块与厂商模块；仅替换 `boot.sd`
会让旧模块与内核不兼容，可能在加载模块时触发 Oops，并阻塞 perf 的符号读取。


### 上板验证

2026-09-17，Cortex-A53 PMU 注册成功，报告 7 个计数器。三秒测试负载取得约
23.96 亿 cycles、19.93 亿 instructions；cache-misses、branch-misses 以及
task-clock/context-switches/page-faults 均能计数。按 99 Hz 进行 cycles 采样，
取得 289 个样本、丢失 0 个；report 能解析 `mix → workload → main` 的调用栈。
原始输出见 `board-test.log`；日志结尾缺少可选 tips 文件的提示已在最终安装中补齐。

### 部署与回退

下面记录的是早期手工部署；当前默认 SD 镜像已包含 perf，无须另行复制。

板上工具位于 `/usr/bin/perf`；测试负载和结果放在 `/mnt/data/perf-test/`。
内核/设备树通过 SD 启动分区的 `boot.sd` 更新，旧版本备份为
`/mnt/data/perf-test/boot.sd.before-perf`。原模块目录备份为
`/mnt/data/perf-test/ko.before-perf.tar`，33 个模块已按新配置重新编译并部署。
本次没有更新 FIP 或重新制作完整 SD 镜像。
回退时把启动镜像备份复制回 SD 第一分区的 `boot.sd`，并用
`tar xf /mnt/data/perf-test/ko.before-perf.tar -C /mnt/system` 恢复匹配的模块，
同步后重启。如果模块异常导致正常重启无法完成，先 `sync` 再 `busybox reboot -f`。

### TideSense A53 1 GHz

TideSense ARM64 SD 构建在 FSBL 中单独将 MPLL/A53 从 800 MHz 设置为 1 GHz，
通过 `TIDESENSE_A53_1GHZ` 隔离，仅匹配 `PROJECT_FULLNAME=sg2002_tidesense_arm64_sd`。
保留 ND 模式的外围时钟，不启用整套 `OD_CLK_SEL`，不修改供电。
板级 DT 同步设置温控频率表为 CPU 1000/500/500 MHz、TPU 700/375/300 MHz，
高温仍可降频，并非锁死在 1 GHz。

本地 SG2002 Preliminary Datasheet V1.0-alpha 同时列出 A53 1 GHz 和 CORE 标称 0.9 V；
不以本项变更要求手动升到 1.0 V。运行稳定性仍应以具体板子的负载测试为准。

```sh
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
build_fsbl
build_kernel
```

更新 SD BOOT 分区根目录的两个文件：

- `install/soc_sg2002_tidesense_arm64_sd/fip.bin`
- `install/soc_sg2002_tidesense_arm64_sd/rawimages/boot.sd`（FIT 格式，勿用上一层 CIMG 同名文件）

重启后检查（设备已挂载 debugfs 时）：

```sh
cat /sys/kernel/debug/clk/clk_summary | grep -E 'clk_mpll|clk_a53'
cat /sys/class/thermal/thermal_zone0/temp
```

`clk_mpll` 和 `clk_a53` 正常最高档应为 `1000000000`；温度单位为毫摄氏度。
设备更新前的启动文件保存在 `/mnt/data/backup-before-1ghz-20260924/`。

2026-09-24 上板短时验证：原电压，时钟树确认 1 GHz；874 帧离线回放完成，
位姿数值与 800 MHz 的同一诊断程序回放一致。VIO 总耗时 32.835→27.548 秒（减少 16.1%），
单帧中位数 35.132→29.401 ms。400 帧双目+IMU smoke 通过：相机 19.954 fps、
IMU 199.537 Hz，配对丢弃/消费替换/IMU 丢弃均为 0；驱动无 SPI 错误或采样间隔异常。
采样温度约 40–41°C；手动检查 cooling state 1→0，A53 正常 500 MHz→1 GHz。
这是短时稳定性测试，不等同于温度/电压全范围长期验证。
日志位于 `install/tidesense-1ghz-validation/`。本次更新了启动文件，未重打整卡镜像。

## 2026-11-02临时超频实验

使用 [clock-trial](clock-trial/README.md) 临时模块，通过内核 CCF 调整 MPLL/A53，
不改电压、DDR、外设时钟或启动镜像。默认仍为 1 GHz，无开机加载项。
模块使用与设备一致的 ARM64 内核构建；温度达到 70°C、读温失败或超时会尝试恢复。
这些保护依赖内核正常执行，不能覆盖 CPU 卡死。

### 离线回放

设备上使用同一个原生 DDS 二进制，关闭 ROS 发布和 JPEG，开启中点积分、像素残差及
双目路标注册。输入为 `dataset-19700102-085210` 的 801 帧双目图像、对应原始 SI IMU，
以及该录制匹配的单级内参配置；没有拿当前设备的新标定替换旧录制的标定。
计时取 `SCHUR_TIMING_CSV` 的 `vio_ms`，仅涵盖 `addImageBundle`，不含图像文件读取。

| 轮次 | VIO 累计耗时 | 单帧中位数 | P95 | 采样最高温度 |
| --- | --- | --- | --- | --- |
| 1.0 GHz，首次 | 25.449 s | 33.248 ms | 51.361 ms | 39.55°C |
| 1.1 GHz | 23.680 s | 31.423 ms | 47.025 ms | 39.90°C |
| 1.0 GHz，重新上电后补测 | 25.598 s | 34.060 ms | 52.310 ms | 40.25°C |

三轮均完成 801 帧、输出 801 个有限位姿，位姿 CSV 的 SHA256 完全相同：
`fe3800620a2270b12e8577890ec400afd07d50f9c1957a80b45c2621ac58187a`。
1.1 GHz 相对首次基线的 VIO 累计耗时减少约 7.0%；相对两次基线均值减少约 7.2%。
这说明本段相同输入下计算更快，不代表改善轨迹精度；没有真值精度评估。
采样频率全程符合各档位，温控状态均为 0。PMU 的 cycles/task-clock 也确认
1.05 GHz 和 1.10 GHz 测试负载的实际频率，不仅依赖时钟树显示。

### 失败及恢复

1.10 → 1.15 GHz 的设置操作后立即失联，未开始 1.15 GHz 的负载测试；
SSH、ping 均不可用，需要人工断电重上电。未取得失联时内核日志，暂不能区分
CPU 超频不稳定与 PLL 在线切换问题。1.20 GHz 没有执行，不能报告为通过。
模块随后将允许上限从 1200 限制为 1100 MHz，已验证写入 1150 被拒绝、频率保持 1 GHz。

补做实时相机测试：1.0 GHz 下 300 帧全部跟踪并融合 IMU，IMU 约 199.54 Hz，
最大间隔 5.044 ms，迟到间隔和队列丢弃均为 0。随后 1.1 GHz 测试在相机启动阶段
以 139 退出，尚未产生 VIO 计时 CSV；日志含 `devmem: not found`、
`family ID request : receive error` 和取图失败。不能仅据此将退出归因于超频，
也不能将 1.1 GHz 标为实时相机验证通过。原定后两轮实时对照没有执行。

1.05 GHz 的 10 秒超时恢复机制已实测，恢复后频率为 1 GHz、MPLL CSR 为
原值 `0x05508101`。实验结束主动恢复并卸载模块，最后再次连接设备确认 A53 为
1 GHz、CSR 与原值一致、模块和实验进程均未运行。没有部署永久超频配置。

原始日志、逐帧 CSV、热监测、构建和分析脚本保存在工程根目录的
`install/tidesense-overclock-20261101/`；目录沿用首次准备实验的日期。

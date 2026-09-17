# TideSense perf

## 构建

在 SDK 根目录执行：

```sh
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
clean_osdrv
build_kernel
build_osdrv
./scripts/perf/build.sh
```

构建脚本使用 SDK ARM64 工具链及 `linux_5.10/tools/perf`，下载并校验
elfutils 0.184（校验值来自 SDK Buildroot），使用 SDK 的 ARM64 zlib。
主机需要 make、gcc、flex、bison、curl、tar、bzip2、gettext 等构建工具。
产物放在 `install/soc_sg2002_tidesense_arm64_sd/tools/perf/`，同时将 perf
安装到该板 rootfs 的 `/usr/bin/perf`，后续打包根文件系统时会包含它。

内核启用 `PERF_EVENTS`、`HW_PERF_EVENTS`、`ARM_PMU`（默认选中）、
`KALLSYMS_ALL`、`STACKTRACE`，保留原有 `FRAME_POINTER`。
板级设备树将 PMU 中断列表修正为单个 A53 对应的 GIC SPI 119。
SDK 默认节点有两个中断、仅一个 affinity 条目，会造成 ARM PMU 注册失败。
内核配置变化后必须同时部署重新编译的内核模块与厂商模块；仅替换 `boot.sd`
会让旧模块与内核不兼容，可能在加载模块时触发 Oops，并阻塞 perf 的符号读取。

## 板上使用（root）

```sh
# CPU 计数器
perf stat -e cycles,instructions,cache-misses,branch-misses -- your_program

# 已运行进程：统计 10 秒
perf stat -p PID -e cycles,instructions -- sleep 10

# 采样与调用栈，输出到可写的数据分区
perf record -F 99 -e cycles -g --call-graph fp \
    -o /mnt/data/perf.data -- your_program
perf report --stdio -i /mnt/data/perf.data

# 实时热点（文本界面）
perf top --stdio -e cycles

# 软件事件
perf stat -e task-clock,context-switches,page-faults -- your_program
```

将 `your_program`/`PID` 换成实际应用。可用
`/mnt/data/perf-test/perf-smoke` 作为三秒测试负载。
应用若需要完整调用栈，应保留符号，并使用 `-fno-omit-frame-pointer`
（ARM64 可加 `-mno-omit-leaf-frame-pointer`）；关闭尾调用优化可保留更多调用层级。

当前工具静态链接，支持 libelf 符号解析、zlib、stat/record/report/top。
不包含 DWARF 展栈、Python/Perl、BPF、图形/TUI 界面；使用 `--stdio` 和
`--call-graph fp`。本次没有开启 scheduler/ftrace 事件，`perf sched` 等需要另配内核。
保持系统 perf 权限设置不变，测试以 root 执行。

`perf version` 显示的 `4.1.6.g...` 来自此 SDK 内核仓库的 Git 标签；
构建源码实际为当前 `linux_5.10/tools/perf`，不是另外下载的 Linux 4.1 工具。

## 上板验证

2026-09-17，Cortex-A53 PMU 注册成功，报告 7 个计数器。三秒测试负载取得约
23.96 亿 cycles、19.93 亿 instructions；cache-misses、branch-misses 以及
task-clock/context-switches/page-faults 均能计数。按 99 Hz 进行 cycles 采样，
取得 289 个样本、丢失 0 个；report 能解析 `mix → workload → main` 的调用栈。
原始输出见 `board-test.log`；日志结尾缺少可选 tips 文件的提示已在最终安装中补齐。

## 部署与回退

板上工具位于 `/usr/bin/perf`；测试负载和结果放在 `/mnt/data/perf-test/`。
内核/设备树通过 SD 启动分区的 `boot.sd` 更新，旧版本备份为
`/mnt/data/perf-test/boot.sd.before-perf`。原模块目录备份为
`/mnt/data/perf-test/ko.before-perf.tar`，33 个模块已按新配置重新编译并部署。
本次没有更新 FIP 或重新制作完整 SD 镜像。
回退时把启动镜像备份复制回 SD 第一分区的 `boot.sd`，并用
`tar xf /mnt/data/perf-test/ko.before-perf.tar -C /mnt/system` 恢复匹配的模块，
同步后重启。如果模块异常导致正常重启无法完成，先 `sync` 再 `busybox reboot -f`。

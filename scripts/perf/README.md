# TideSense perf

电脑端命令均在工程根目录执行；“板上使用”章节的命令在设备上执行。

## 构建

TideSense `build_all` 默认构建 perf，SD 镜像预装 `/usr/bin/perf`，测试负载和许可文件位于 `/mnt/data/perf/`。
以下步骤用于单独构建或更新内核：

在 SDK 根目录执行：

```sh
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
clean_osdrv
build_kernel
build_osdrv
./scripts/perf/build.sh
```

## 板上使用（root）

```sh
# CPU 计数器
perf stat -e cycles,instructions,cache-misses,branch-misses -- your_program

# 已运行进程：统计 10 秒
perf stat -p PID -e cycles,instructions -- sleep 10

# 采样与调用栈，输出到可写的数据分区
perf --buildid-dir /mnt/data/perf-cache record -F 99 -e cycles -g --call-graph fp \
    -o /mnt/data/perf.data -- your_program
perf --buildid-dir /mnt/data/perf-cache report --stdio -i /mnt/data/perf.data

# 实时热点（文本界面）
perf --buildid-dir /mnt/data/perf-cache top --stdio -e cycles

# 软件事件
perf stat -e task-clock,context-switches,page-faults -- your_program
```

将 `your_program`/`PID` 换成实际应用。可用
`/mnt/data/perf/perf-smoke` 作为三秒测试负载。
采样和符号缓存均放到 DATA 分区：只指定 `-o /mnt/data/perf.data` 时，perf 默认仍会
把符号缓存写到 `/root/.debug`；本板一次验证约占 61 MiB。上面的 `--buildid-dir`
避免持续占用较小的 ROOTFS，采样和报告应使用相同目录。
应用若需要完整调用栈，应保留符号，并使用 `-fno-omit-frame-pointer`
（ARM64 可加 `-mno-omit-leaf-frame-pointer`）；关闭尾调用优化可保留更多调用层级。

当前工具静态链接，支持 libelf 符号解析、zlib、stat/record/report/top。
不包含 DWARF 展栈、Python/Perl、BPF、图形/TUI 界面；使用 `--stdio` 和
`--call-graph fp`。没有开启 scheduler/ftrace 事件，`perf sched` 等需要另配内核。
保持系统 perf 权限设置不变，测试以 root 执行。

`perf version` 显示的 `4.1.6.g...` 来自此 SDK 内核仓库的 Git 标签；
构建源码实际为当前 `linux_5.10/tools/perf`，不是另外下载的 Linux 4.1 工具。

上板、调频及早期部署记录见 [VALIDATION.md](VALIDATION.md)。

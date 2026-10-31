# A53 CPU 热点采样

结果与解释见 [HOTSPOTS.md](HOTSPOTS.md)。脚本从 SDK 根目录运行，依赖当前 GCC 11 静态构建的 stage（先运行 `scripts/schurvins/build.sh`），以及板端 perf。

板端准备 `/mnt/data/schur-profile/calib.yaml` 和 `stereo.csv`，内容与 benchmark 一致；图像路径必须为板上绝对路径。IMU 输入固定为 `/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay/mav0/imu0/data.csv`。现有 `/mnt/data/schur-opt-test` 配置可复制使用。配置 SSH key，或设置 `SSHPASS` 并安装 sshpass；`BOARD` 默认为 `root@192.168.123.99`。

```sh
./scripts/schurvins/profile/build.sh
./scripts/schurvins/profile/record-ram.sh base
./scripts/schurvins/profile/record-ram.sh all
# PERF_HOST 可指定能运行的宿主机 perf；VMLINUX 可指定与记录 build-id 一致的内核 ELF。
./scripts/schurvins/profile/decode.sh base
./scripts/schurvins/profile/decode.sh all
```

必须串行运行，期间避免其他板端负载及文件上传。`record-ram.sh` 将 perf、日志、位姿及计时全部写入 tmpfs，结束后才拉回宿主机。会覆盖专用测试目录中的程序和同配置结果；不替换生产安装。

`SCHUR_PROFILE_CSV` 启用程序的可选计时接口：输出每次 `addImageBundle` 的 CLOCK_MONOTONIC 区间和 CLOCK_PROCESS_CPUTIME_ID 差值，CSV 写入在区间之外。未设置时不执行新增的 CPU 时钟测量。解码脚本排除前五帧，只统计这些 VIO 区间内的采样，以 cycles period 加权，分开报告 self、inclusive 和内核占比；inclusive 不能相加，采样数不是函数调用次数。

应用 ELF 没有 GNU build-id，必须保留同次链接的未 strip 文件。脚本保存它并通过 symfs 解析。原始 perf.data、符号及完整日志在 `install/schurvins-gcc11-static/profile/`，本次汇总与逐帧区间另保存在本目录 `results/`。检查 decode.log、event-stats.txt 和栈覆盖率后再解释结果。

`record-sync-diagnostic.sh` 仅用于复现同步 SD 输出造成的阻塞，不能用于正常的计算性能比较；它有意保留旧输出位置。

## align2D 优化后的四项全开版本

最新结果见 [HOTSPOTS-ALIGN2D.md](HOTSPOTS-ALIGN2D.md)。`base` 表示四项全关，
`all` 保留历史含义（前三项 ON、align2D OFF），`align` 表示四项 ON。

```sh
./scripts/schurvins/profile/build.sh align
./scripts/schurvins/profile/record-ram.sh align
./scripts/schurvins/profile/decode.sh align
```

历史二进制来源见 [VALIDATION.md](VALIDATION.md)。

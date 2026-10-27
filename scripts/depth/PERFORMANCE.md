# 双目深度实时模式 perf 分析（2026-10-23）

本文记录优化前版本；优化后的默认配置、性能和正确性验证见
[OPTIMIZATION.md](OPTIMIZATION.md)。下文的“当前模式”指本次基线采样时的版本。

结论：**默认 10 Hz 是请求限频；640×400 的低帧率主要来自 StereoBM，
并被单核上的采集、内核和 ROS 通信竞争进一步拉低。** 当前模式使用标准 OpenCV，
没有启用旧实验的 StereoBM 优化。另一个明确热点是相机从未缓存图像映射执行 memcpy。

## 实际吞吐量

设备 SG2002 A53 单核，采样前后均确认 `clk_a53=1 GHz`，温度约 40–42°C，
没有观测到降频。双目原始采集约 20.39 Hz。模式参数均请求 `--fps 20`，D32 / D64。

| 条件 | 分辨率 | 处理/接收速率 |
| --- | --- | --- |
| 正式程序，micro-ROS 开启，不运行 perf | 320×200 | **18.92 Hz**，处理 400 帧，接收窗口内 399 帧 |
| perf 采样，micro-ROS 开启 | 320×200 | 稳态处理约 17.49 Hz，全段接收约 17.06 Hz |
| perf 采样，micro-ROS 开启 | 640×400 | 稳态处理约 **2.42 Hz** |
| perf 采样，Agent 不在线 | 640×400 | 稳态处理约 **2.68 Hz** |

未采样的正式 320 程序平均计算阶段墙钟时间为 50.72 ms。
perf 自身占 CPU，尤其双事件采样加计数器统计会降低吞吐量；不能把采样速率
直接当成正式程序的最高速率。上次请求 4 Hz 的全分辨率正式程序约 2.69 Hz，见
[VALIDATION.md](VALIDATION.md)。

Agent 不在线时保留相机、深度计算和单帧输出队列，但没有图像网络发送。
全分辨率仍明显低于 4 Hz，说明单纯调 ROS QoS 或增加网络带宽无法解决主要瓶颈。

## 计算阶段在哪里花时间

下面的百分比是**主线程计算阶段的用户态 cycles 采样周期占比**，
排除前 10 帧、采集等待、后台线程和内核；三阶段可相加。

| 阶段 | 320×200 / D32 | 640×400 / D64 |
| --- | --- | --- |
| 双目 remap 校正 | 25.03% | 10.91% |
| StereoBM（含预处理及验证过滤） | **73.30%** | **88.43%** |
| 视差转毫米深度 | 1.66% | 0.66% |

在 StereoBM 阶段内部，`findStereoCorrespondenceBM_SIMD<short>` 自身占
**89.68% / 95.39%**。全分辨率的散斑过滤仅占 BM 阶段约 1.80%，
左右一致性验证约 1.56%。优先删除这些质量过滤不能带来主要收益。

全分辨率主线程计算阶段的 L1 数据缓存 refill 采样周期约 93.54% 位于 BM。
反汇编热点仍是差分缓冲更新、滑动 SAD 累加与最小值搜索；高权重地址包括
`0x435468`、`0x43547c` 一带的广播/向量加载及其相邻更新指令，
与 [10 月 13 日的实验](../vio-compare/probes/stereo-bm/results/perf-20261013.md) 一致。
PMU 采样有指令归因偏移，缓存 refill 比例不能解释为 DRAM 等待时间比例。

## CPU 时间与实际耗时的差别

同一次真实输入中同时测量 `CLOCK_MONOTONIC` 和 `CLOCK_THREAD_CPUTIME_ID`。
下面是采样窗口内完整帧的平均值：

| 配置 / 阶段 | 校正，CPU / 实际 ms | BM，CPU / 实际 ms | 深度转换，CPU / 实际 ms | 合计，CPU / 实际 ms |
| --- | --- | --- | --- | --- |
| 320×200 + ROS | 8.89 / 16.85 | 25.64 / 38.37 | 0.49 / 0.76 | **35.02 / 55.98** |
| 640×400 + ROS | 32.01 / 59.15 | 248.88 / 351.49 | 1.86 / 2.24 | **282.75 / 412.88** |
| 640×400，Agent 不在线 | 30.16 / 40.43 | 246.60 / 330.37 | 1.89 / 2.49 | **278.64 / 373.29** |

单核在采样窗口中接近 100% 忙。全分辨率 ROS 开启时：主线程占核心约 65.8%，
ROS 线程约 5.3%，图像接收线程约 2.0%，相机辅助进程约 18.0%；
剩余还有 ISP/IMU/USB 等内核任务及 perf 自身。
这里的 CPU 占用由 `/proc` tick 差分估计，包含各任务的用户态和内核态。

因此“每帧需要约 283 ms CPU”会变成“约 413 ms 才完成”，
不能直接用离线单进程的计算容量推算实时输出帧率。
图像数量增加 4 倍、搜索视差增加 2 倍，也使全分辨率匹配工作量约增至 8 倍。

## 全系统采样发现的采集拷贝热点

额外运行一组 640×400、Agent 不在线的全系统 `cycles` 采样，包含内核。
相机辅助进程主线程中 **81.35% 的周期落在 libc memcpy**，
该进程的 memcpy 约占本次全系统采样周期的 10.84%。

采集源码 `scripts/schurvins/camera_source.c` 仍使用 `CVI_SYS_Mmap` 后逐行复制，
该 SDK 接口是未缓存映射。20 Hz 下即使深度只消费约 2.7 帧/秒，
辅助进程仍持续复制和发送全部双目图像。
UVC 的 `scripts/uvc/camera.c` 已使用 `CVI_SYS_MmapCache` 加
`CVI_SYS_IonInvalidateCache`，值得复用这一已存在的实现方式。
缓存修改必须保留 DMA 一致性处理，并单独验证图像完整性。

## 优化优先级

1. **复用已验证的 StereoBM 优化路径。** 旧实验在 640×400 使用 all64
   （限制 stripe 行数、向量 widening、重新计算出窗差分），885 帧原始视差均与基线一致；
   离线总计算时间由 252.45 ms 降到 167.76 ms，约减少 33.55%。
   当前深度模式尚未启用这些优化，需要在当前标定、实时输入和深度有效性 mask 下再验证。
2. **降低相机未缓存 memcpy 的开销。** 参照 UVC 的缓存映射实现，释放单核 CPU 给匹配。
   如果全分辨率仍只计算少量帧，还可进一步研究只复制被选中图像的采集接口。
3. **随后处理 ROS 与复制链路。** 320×200 接近 20 Hz 时，通信线程占核心约 10.8%，
   值得继续优化；全分辨率下它是次要因素。

上述收益分别来自已有实验和本次热点定位，未承诺实时帧率提升幅度。
本次只增加可选计时和采样工具，没有替换正式程序或修改默认限频、匹配算法、质量过滤。

## 复现与产物

新增工具：`scripts/depth/profile-board.sh`、`scripts/depth/analyze-profile.py`。
先用 `scripts/depth/build.sh` 构建；未剥离 ELF 保留在
`install/tidesense-depth-build/tidesense_depth`。
将该 ELF 的 stripped 副本及采样脚本放入板端独立目录，**必须保存同一链接产物的原始 ELF**。

电脑端启动已有 Humble micro-ROS Agent 和 `receive.py`，然后在板端执行：

```sh
SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  sh /mnt/data/depth-perf-20261023/profile-board.sh \
  /mnt/data/depth-perf-20261023 640 example-640 18
```

换用未运行 Agent 的端口可以测无发送对照。设置 `DEPTH_PERF_SYSTEM=1`
可做包含内核的全系统 cycles 采样，脚本会同步保存当前 `/proc/kallsyms`。
普通采样为 `cycles:u,L1-dcache-load-misses:u`、每事件 499 Hz，
采样数据和阶段 CSV 先写 tmpfs，停止应用后复制到 DATA 分区。
同时通过 `perf stat -t MAIN_TID` 记录计数；全分辨率主线程约 0.41 IPC，320 模式约 0.64 IPC。

主机使用 `/usr/lib/linux-tools/5.15.0-198-generic/perf` 解码，symfs 中
为板端 ELF 路径建立指向精确 unstripped ELF 的映射：

```sh
perf script --ns -F time,pid,tid,period,event,ip,sym,dso \
  --symfs SYMFS -i CASE/perf.data > CASE/samples.txt
python3 scripts/depth/analyze-profile.py CASE
```

全系统解码另加 `--kallsyms CASE/kallsyms`，symfs 中放置精确相机 ELF 和板端 libc。
解码报告未见 LOST 记录；最终用户态样本未解析权重低于 0.16%，
全系统样本未解析权重约 0.067%。采集 ELF 重建后 strip 的哈希与正式采集程序一致。

本次产物位于 `install/depth-perf-20261023/`，板端对应目录为
`/mnt/data/depth-perf-20261023/`：

- `RUN-INFO.json`、`loss-audit.json`：采样方法、源文件/ELF 哈希、记录丢失检查。
- `320-20hz-profile-final/`、`640-20hz-ros/`、`640-20hz-noagent/`：
  原始 perf.data、阶段 CSV、时钟/温度及 `/proc` 快照、解码样本和 summary.json。
- `640-system-noagent/`：全系统记录、当时的 kallsyms、包含相机 memcpy 的线程排名。
- `640-20hz-ros/annotate.txt`：匹配内核的 AArch64 反汇编采样。
- `320-20hz-production-*`：未采样的正式程序及接收器日志、图像和统计。

主机和 A53 的几何测试均通过，包括校验开启/关闭阶段计时后的深度图逐像素相同。
采样结束后 FSIN=0、IMU=200 Hz，所有测试进程已退出。
本次输入仍几乎只有噪声，覆盖率不能用于评估真实测距精度；
性能也应在正常照明及纹理场景下复测。

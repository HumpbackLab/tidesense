# C906 独占硬件 IVE 探针

验证 TideSense 的 C906 直接执行 Hist 和 ST 候选响应图，不修改 Linux
设备树、内核镜像或 SD 启动镜像。`front_firmware.h` 还提供可选的 OpenVINS
完整前端回调，配套编译、运行和吞吐对照见
[`../../openvins/README-frontend.md`](../../openvins/README-frontend.md)。

硬件 IVE 是 `0x0a0a0000`、大小 `0x3100` 的独立引擎，与 TPU 图像库不同。
现有设备树和 `clk_ive_vip` 的 critical 时钟足够使用。桥接模块 `ive=1`
要求 Linux IVE 驱动已卸载，并独占预留现有设备的寄存器区域。

`generate_sdk.py` 从 SDK `cvi_ive_platform.c` 抽取同一套寄存器配置和宽图分块
逻辑，生成仅用于启动配置的 A53 C 代码；不在每帧调用 Linux IVE ioctl。
执行表包含复位、清状态、DMA 配置、开始和有超时的完成检查，ST 饱和重算和
动态归一化寄存器在小核读取实际硬件值后处理。C906 在首次 MMIO 写入前检查
整张指令表；单阶段等待限 20 ms、整张表限 100 ms，超时执行局部软复位。

共享主缓冲复用前端 12 MiB 布局，IVE 程序／直方图／临时图／输出另分配
4 MiB ION，不扩大小核 reserved-memory。两个功能编译开关默认均关闭。
实验固件只将启动栈从前端版的 96 KiB 调至 88 KiB，任务堆和任务栈不变；
独立探针 ELF text+data+bss 为 776,954 字节；加入对齐前端回调和分段计时的
实验固件为 779,070 字节，均位于现有 768 KiB 范围内。
这不是对其他组合开关下固件尺寸的保证。

## 构建

```sh
# 仓库根目录；先完成 TideSense SDK 的原有编译。
C906_OUT="$PWD/install/openvins-c906-frontend/ive-c906/tools" \
C906_FRONT_IVE_HIST=ON C906_FRONT_IVE_ST=ON C906_LK_DUAL_BATCH=ON \
scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/probes/c906-ive/build_probe.py
```

将生成的 `tools/`、`ive-probe.a53` 和
`scripts/vio-compare/openvins/with-tpu.sh` 复制到设备的独立实验目录。
`tools/` 还需要保留原来的、支持热重载的 `cvirtos-reload-base.bin`，用于退出
恢复。不要用实验固件替换该文件。当前设备实验目录为
`/mnt/data/openvins-c906-front/ive-c906`；正式应用没有被替换。

## 运行

输入是连续行 U8 灰度原始文件，每像素一字节，无 PGM 文件头。所有尺寸和
重复次数都通过命令行传入，最多 640×480、64 次。原始分辨率不同则需先转换。

```sh
cd /mnt/data/openvins-c906-front/ive-c906

# Linux 原驱动生成对照；必须在独占包装器之外执行。
./ive-probe.a53 st-linux 640 480 fpv-1000.raw 1 0 reference.bin

# 只读 C906 侧 IVE 寄存器。
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_IVE=1 \
  ./with-tpu.sh ./ive-probe.a53 read

# C906 Hist，检查全部 256 桶。
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_IVE=1 \
  ./with-tpu.sh ./ive-probe.a53 hist 640 480 fpv-1000.raw 20 0

# C906 ST，逐像素对照 Linux 结果。
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_IVE=1 \
  ./with-tpu.sh ./ive-probe.a53 st 640 480 fpv-1000.raw 20 0 reference.bin

# 模拟真实金字塔：输入有 18 像素边框，stride=width+36；小核先整理到
# 连续对齐缓冲，再启动 IVE。device_ms 包含整理；pack_mean_ms 单列整理成本。
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 OPENVINS_TPU_IVE=1 \
  ./with-tpu.sh ./ive-probe.a53 st 640 480 fpv-1000.raw 20 2 reference.bin
```

倒数第二个参数是输入布局：0 为对齐连续输入，2 为 C906 整理非对齐输入。
布局 1 只用于早期非对齐访问诊断，不是可用 ST 路径，不能将其 Linux 输出作为
有效参考。正常运行及失败退出后，包装器均尝试恢复原小核固件和原 Linux
TPU／IVE 模块；运行期间这些引擎不能同时被其他应用使用。

`device_ms` 使用小核 25 MHz 时钟，包括寄存器表解释和完成轮询，不包含
一次性分配／表生成及主机结果检查。重复测试是在一次 mailbox 内连续执行，
最后才由主机校验结果，不能将主机 CPU 时间直接当作逐帧提交开销。
探针检查输出后方及临时图外的 DMA 保护区。ST 的参考是 SDK 硬件响应图，
不是最终选点列表，也不是 OpenCV Shi–Tomasi 数学参考。

2026-10-30 的 14 组输入、42 组各 20 次测试及结果见
[`../../openvins/README-frontend.md`](../../openvins/README-frontend.md)。
640×480 单图 Hist 约 1.107 ms，ST 约 2.452 ms，输入整理＋ST 约 3.602 ms。
这些是独立探针耗时；完整前端实验使用直接对齐输出，不需要该整理步骤。
本轮完整 VIO 不做轨迹精度回归，吞吐对照见上面的前端文档。

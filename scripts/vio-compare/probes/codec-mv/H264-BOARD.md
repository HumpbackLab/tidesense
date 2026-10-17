# H.264 板端直接读取验证

## 结果

已在 TideSense SG2002 板上运行独立 H.264 探针，共完成 **100 帧编码**：

- 三组完整缓冲区快照：整幅 `(8,4)` 平移 16 帧、逐帧变向 12 帧、四象限不同运动 12 帧。
- 五组直接读取运动上下文：逐帧变向、四象限、亚像素平移、仅右下角运动、仅左上角运动，
  各 12 帧。后两组只移动一个 80×80 区域，其余图像静止。

**发现并验证了四组运动相关的上下文值；完整的逐分区 MV 表仍未获取到。**
它们不等同于 [H264.md](H264.md) 中电脑解码导出的每 P 帧约 1143 条向量，
目前不能把那轮初值覆盖率和 LK 节省直接用于板端。

本轮板端没有运行 FFmpeg，没有软件解码，也没有修改编码器驱动、固件和默认 VIO。

## 读取了什么

在 `CVI_VENC_GetStream` 成功后，下一次 SendFrame 之前，只读映射本通道的 ION 分配：

| 分配 | 每个缓冲区大小 | 数量 |
|---|---:|---:|
| `VENC_0_H264_WorkBuffer` | 81920 B | 1 |
| `VENC_0_ReconFrameBuf` | 720896 B | 2 |

所有地址都从当前 ION 列表发现，并限制在本板保留内存范围；没有写寄存器。
这轮物理地址为工作区 `0x8b961000`、重建区 `0x8b975000` 和 `0x8ba25000`，
不把它们作为固定接口地址。

在工作区偏移 `0x400`、`0x404`、`0x408`、`0x40c` 发现四个 32 bit 数值：

```text
高 16 bit：有符号 x
低 16 bit：有符号 y
观测到的方向：当前帧指向参考帧
观测到的单位：1/4 像素
```

这是本轮实验识别的格式，不是公开稳定 ABI，也没有完整的空间位置、分区、参考帧和有效性描述。
I 帧上的数值不应当作有效运动初值。

## 已知运动验证

`0x404` 这组值在以下测试中表现为：

| 测试 | 实际运动 px | 读到的原值 | 转换后 px |
|---|---|---|---|
| 整幅平移 | `(8,4)` | `(-32,-16)` | `(8,4)` |
| 逐帧变向中的下一帧 | `(-6,2)` | `(24,-8)` | `(-6,2)` |
| 亚像素平移 | `(1.5,0.75)` | `(-6,-3)` | `(1.5,0.75)` |
| 仅右下角 80×80 区域运动 | `(-4,-3)` | `(16,12)` | `(-4,-3)` |
| 仅左上角 80×80 区域运动 | `(-4,-3)`，右下角静止 | `(0,0)` | `(0,0)` |

逐帧变向及亚像素序列的全部 11 个 P 帧，`0x404` 与整幅预期位移完全一致，
未观察到延迟一帧的现象。局部运动测试中，该值跟随右下角区域；
不能把它解释为全画面运动估计。它是否属于最后宏块、ME 候选或其他固件上下文，
本轮没有确定，不能凭这些结果断言具体内部语义。

四象限不同运动时，另外三组数值有时对应局部运动，有时为零或偏差较大；
没有验证它们分别对应固定四个象限，因此也不能给四个值绑定四个画面区域。

保持映射后仅复制这 16 B，计时约 1.4–1.5 µs，包含计时函数的影响。
这不包含硬件编码、初次查 ION 和映射、CSV 输出，也不是完整向量表的提取开销。

## 完整 MV 导出目前缺少什么

公开的 `VENC_STREAM_S` 没有逐块 MV；当前 H.264 编码也没有独立的已验证 MV 分配。
检查 SDK 编码路径发现：

- `vpuapi.h` 定义了 `EncReportInfo mvInfo`，文档提及 `ENC_SET_REPORT_MVINFO`。
- 当前 `Coda9VpuEncode()` 的 `CMD_ENC_PIC_OPTION` 只设置 fieldRun、force-I/skip 等现有参数，
  没有开启 MV report。
- `CMD_ENC_PIC_PARA_BASE_ADDR` 有寄存器定义，但当前编码路径没有配置它来输出 MV report。
- `Coda9VpuEncGetResult()` 没有读取逐块 MV report 的实现。

本轮快照没有识别出可用的密集 MV 网格；这不足以证明固件或芯片绝对不能导出。
要获得完整表，需要进一步验证固件支持及报告控制位、缓冲区结构，然后在驱动中提供接口。
现有头文件的说明本身不能证明当前固件已支持这条路径。

目前可明确区分：H.264 硬件能编码，运动相关上下文可直读；
**带位置和有效性信息的完整 H.264 MV 导出仍未打通**。

## 文件与复现

本机产物目录 `install/venc-h264-direct-20261017/`：

- `sample_h264_probe`：独立静态探针。
- `shift-8-4/`、`dynamic/`、`quadrants/`：编码日志、码流及 120 个 ION 缓冲区快照。
- `context-*/words.csv`：五种测试的四组上下文读数。
- `probe-summary.json`：帧数、快照检查、已知运动比较及读取计时。
- `board-snapshots.sha256`：板端快照校验值。

构建及生成局部运动图：

```bash
python3 scripts/vio-compare/probes/codec-mv/build_h264_probe.py
/usr/bin/python3 -S scripts/vio-compare/probes/codec-mv/h264_local_motion.py
```

依赖之前 `build_device.py` 生成的 `install/openvins-codec-shallow/device/` sample 对象，
以及 `make_device_input.py` 生成的已知运动 YUV。构建只生成独立探针，不覆盖 SDK 原对象。

板上无其他多媒体程序时，将探针及生成的 YUV 复制到 `/mnt/data/venc-h264-direct-20261017/`。
完整快照示例：

```sh
cd /mnt/data/venc-h264-direct-20261017
mkdir -p dynamic
VENC_H264_PROBE_DIR="$PWD/dynamic" ./sample_h264_probe \
  -c 264 -w 640 -h 400 -i /mnt/data/venc-mv-probe-20261017/dynamic.yuv \
  -o dynamic/stream -n 12 --rcMode=4 --iqp=20 --pqp=20 \
  --gop=60 --gopMode=0 --srcFramerate=20 --framerate=20
/mnt/data/openvins-codec-shallow/cleanup_idle --probe-idle
```

只记录四组上下文时，将 `VENC_H264_PROBE_DIR` 替换为
`VENC_H264_CONTEXT_CSV="$PWD/context-dynamic/words.csv"`，预先建立输出目录。
回传全部测试结果后运行：

```bash
/usr/bin/python3 -S scripts/vio-compare/probes/codec-mv/h264_probe_summary.py
```

编码结束确认 ION 使用为 0。120 个快照在本机核对 SHA256 全部一致后，
清理了板上的临时快照和新生成的两份局部运动 YUV；原始测试图及本机产物保留。

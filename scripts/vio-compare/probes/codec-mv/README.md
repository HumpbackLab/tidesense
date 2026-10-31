# 编码运动向量作为 LK 初值：本机实验

以下电脑端命令均在工程根目录执行。

只改 OpenVINS 前端的初始位置：

```text
前一帧原始灰度图 ── H.264 编码运动向量 ── 当前帧粗位置
                                         ↓
前后帧原始灰度图 ── 全金字塔 LK 亚像素匹配 ── 原有 RANSAC ── FP64 后端
```

这是独立的本机实验，不接入默认构建，不修改设备程序。
编译宏 `OPENVINS_CODEC_MV_EXPERIMENT=1` 隔离新增的跟踪代码；实验程序的运行开关
`OV_CODEC_MV_MODE` 默认是 `off`。

## 运行

依赖本机已有的 FP64 基准库、OpenCV 静态库、GCC 11、FFmpeg 开发库和系统 NumPy。
`build.py` 读取现有构建的编译及链接参数，只替换复制库中的 `TrackKLT.cpp.o` 和回放适配器。
现有库路径写在脚本开头；不是独立从零构建 OpenVINS 的脚本。

```bash
python3 scripts/vio-compare/probes/codec-mv/prepare.py
python3 scripts/vio-compare/probes/codec-mv/build.py

# 主配置：完整序列，三次串行重复，各次交替测试顺序。
python3 scripts/vio-compare/probes/codec-mv/run.py --caps 1000 --repeat 3

# 同时检查此前保留的 RANSAC 200 上限开关。
python3 scripts/vio-compare/probes/codec-mv/run.py --caps 200
/usr/bin/python3 -S scripts/vio-compare/probes/codec-mv/summarize.py
```

单独运行 TideSense 的覆盖块初值版本：

```bash
work=install/openvins-codec-mv
OV_CODEC_MV_MODE=rect \
OV_CODEC_MV_BIN="$work/tide/vectors.bin" \
OV_CODEC_MV_DETAIL="$work/tide-example-detail.csv" \
OV_CODEC_IMU_AHEAD=0.006 OMP_NUM_THREADS=1 \
  "$work/build/run_record.r1000" \
  "$work/tide/config/estimator_config.yaml" \
  install/replay-20261012-084638/record \
  "$work/tide-example-poses.csv" 0
```

改为 `OV_CODEC_MV_MODE=off` 即关闭初值替换，也不会读取或构建向量索引。
FPV 使用 `fpv/config`、`fpv/vectors.bin`、
`install/fisheye-benchmark/openvins/uzh-input`，以及 `OV_CODEC_IMU_AHEAD=0.03`。
适配器只支持本实验的单目输入，避免误用右目坐标。

## 结果和限制

具体结果见同目录 `RESULTS.md`。原始日志、逐帧轨迹、前端分项时间、编码命令和统计位于
`install/openvins-codec-mv/`。`summary.json` 保留逐次结果，`aggregate.json` 汇总重复结果。

本实验先离线编码并缓存向量，**不能把表中的 VIO 时间当作在线端到端耗时**。
SG2002 的后续板端直接读取验证见 [DEVICE.md](DEVICE.md)：已取得并解析 H.265 私有 MV 表，
尚未封装正式驱动接口，也未验证去除诊断后的获取成本。
软件解码导出向量是可验证的输入来源，未证明它在板端能加速。

后续使用实际板端向量的“两层 LK＋失败点补跑”实验见 [SHALLOW.md](SHALLOW.md)。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

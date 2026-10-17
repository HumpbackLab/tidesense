# 编码运动向量作为 LK 初值：本机实验

只改 OpenVINS 前端的初始位置：

```text
前一帧原始灰度图 ── H.264 编码运动向量 ── 当前帧粗位置
                                         ↓
前后帧原始灰度图 ── 全金字塔 LK 亚像素匹配 ── 原有 RANSAC ── FP64 后端
```

这是独立的本机实验，不接入默认构建，不修改设备程序。
编译宏 `OPENVINS_CODEC_MV_EXPERIMENT=1` 隔离新增的跟踪代码；实验程序的运行开关
`OV_CODEC_MV_MODE` 默认是 `off`。

## 实现

- `prepare.py` 使用本机 FFmpeg/libx264 编码左图并导出运动向量。
  固定单参考帧、无 B 帧、无前视、关闭场景切换 I 帧，GOP 60、CRF 20。
  每个 P 帧只使用前一输入帧；不使用未来图像。
- FFmpeg 的向量方向是当前块到参考块。使用完整的
  `motion_x / motion_scale`、`motion_y / motion_scale`，将块覆盖区域映射到前一帧，
  在该区域查询特征点，再取相反方向预测当前坐标。没有使用已取整的 `src_x/src_y`。
- `rect`：选择覆盖特征点、且源块中心最近的向量。
- `median`：查询邻近 24 px 范围的向量，取横纵分量中值；局部分歧过大时放弃预测。
  不足 3 个邻近向量时使用覆盖块。
- I 帧、无可用向量及预测出界时，使用原来的 LK 初值。
- LK 仍为 16×15 窗口、原有全部金字塔层、30 次迭代上限、0.01 终止阈值。
  保留 LK 失败点过滤，保留相同的归一化、RANSAC、特征 ID 管理和后端。
- 向量缓存包含当前时间戳、前一帧时间戳、帧序号和记录数，回放时严格核对。
  相机及 IMU 时间戳、标定、噪声、采样帧数均保持原配置。
- VIO 使用原始 PGM，不使用有损解码图。实验验证编码向量能否作为原图的初值。

`extract_mvs.c` 基于 [FFmpeg extract_mvs 示例](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/extract_mvs.c)，
保留原 MIT 许可，增加完整运动分量和精度分母的输出。

## 运行

依赖本机已有的 FP64 基准库、OpenCV 静态库、GCC 11、FFmpeg 开发库和系统 NumPy。
`build.py` 读取现有构建的编译及链接参数，只替换复制库中的 `TrackKLT.cpp.o` 和回放适配器。
现有库路径写在脚本开头；不是独立从零构建 OpenVINS 的脚本。

```bash
cd /home/ncer/sophpi
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
work=/home/ncer/sophpi/install/openvins-codec-mv
OV_CODEC_MV_MODE=rect \
OV_CODEC_MV_BIN="$work/tide/vectors.bin" \
OV_CODEC_MV_DETAIL="$work/tide-example-detail.csv" \
OV_CODEC_IMU_AHEAD=0.006 OMP_NUM_THREADS=1 \
  "$work/build/run_record.r1000" \
  "$work/tide/config/estimator_config.yaml" \
  /home/ncer/sophpi/install/replay-20261012-084638/record \
  "$work/tide-example-poses.csv" 0
```

改为 `OV_CODEC_MV_MODE=off` 即关闭初值替换，也不会读取或构建向量索引。
FPV 使用 `fpv/config`、`fpv/vectors.bin`、
`install/fisheye-benchmark/openvins/uzh-input`，以及 `OV_CODEC_IMU_AHEAD=0.03`。
适配器只支持本实验的单目输入，避免误用右目坐标。

## 测试口径

- 本机、单 OpenCV 线程、纯 FP64 后端、150 点、无 ROS。
- TideSense `dataset-20261012-084638`：完整 885 帧、647 帧初始化后状态。
  使用原标定、3 px 视觉噪声和 IMU intrinsic 在线校准。
- UZH-FPV `indoor_forward_6`：完整 1970 帧、1782 帧初始化后状态。
  使用原 FPV 配置和 1.5 px 视觉噪声。
- `off` 的四组配置与已有本机基线比较：全部时间戳、初始化标记和输出位姿数值完全一致。
- 主配置每项重复三次；相同配置的全部重复位姿完全一致。
- 相机回调时间包含向量缓存读取、建索引、查询初值、诊断及原来的相机处理；
  不含图像读取、回调外 IMU 输入和编码/解码导出。表中取三次均值的中位数，
  统计初始化后且排除首次初始化帧的相机回调。
- LK/RANSAC 分项时间按完整输入帧数折算，包含初始化前的跟踪；
  统计区间与上一条相机回调不同，不能直接相加。
- FPV ATE 在各版本共同的 816 个真值覆盖时间戳上统计，覆盖约 30.15 秒；
  相机时间换算到 IMU 真值时间使用官方 -16.684572 ms。
  使用 SE(3) 对齐，**不拟合尺度**。该真值区间不能代表完整 65 秒轨迹的误差。
- TideSense 无外部真值，首末位置差只是回到起点录像的漂移参考，不能替代 ATE。

## 结果和限制

具体结果见同目录 `RESULTS.md`。原始日志、逐帧轨迹、前端分项时间、编码命令和统计位于
`install/openvins-codec-mv/`。`summary.json` 保留逐次结果，`aggregate.json` 汇总重复结果。

本实验先离线编码并缓存向量，**不能把表中的 VIO 时间当作在线端到端耗时**。
SG2002 的后续板端直接读取验证见 [DEVICE.md](DEVICE.md)：已取得并解析 H.265 私有 MV 表，
尚未封装正式驱动接口，也未验证去除诊断后的获取成本。
软件解码导出向量是可验证的输入来源，未证明它在板端能加速。

后续使用实际板端向量的“两层 LK＋失败点补跑”实验见 [SHALLOW.md](SHALLOW.md)。

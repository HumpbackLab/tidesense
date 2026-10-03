# A53 UZH-FPV perf：完整 FP32 后端加打包

## 测量范围

TideSense SG2002 A53 1 GHz，GCC 11.3 静态程序，OpenCV 3.4.20，
无 ROS，单 OpenCV 线程，`OPENVINS_FP32_PACKED_MA=1`，关闭 active visualization。
输入仍为 `indoor_forward_6` 的完整 1970 帧双目及 500 Hz IMU。

为了核实前端 FP64 的实际开销，使用独立回放适配器的 perf FIFO 控制通道，
仅在 `feed_measurement_camera()` 前启用、后禁用采样。
从第 201 帧开始采样：初始化成功在第 189 帧，前 200 帧用于运行算法但不采样。
PNG 解码、IMU 缓存输入、输出 CSV 和每帧数值检查均在采样范围外。
没有设置 `OPENVINS_NUMERIC_EIGEN`，周期性协方差特征值诊断也关闭。

使用 `cycles:u`、99 Hz、frame-pointer 调用链，得到 **14330 个样本，零丢失**，
加权周期总数为 141,289,247,055。控制函数的捕获调用链只占约 0.007%。
没有 PNG 解码或额外特征值诊断样本。
OpenCV 编译命令中最终的 `-fomit-frame-pointer` 会省略部分中间调用帧，
所以调用链归属只能用于识别已捕获的路径，不能当作完整的逐阶段时间分解。
表内使用函数自身的周期，避免累计调用链重复计数。

## 热点

| 函数/函数组 | 视觉处理采样周期占比 | 当前主要计算类型 |
| --- | ---: | --- |
| `cv::detail::LKTrackerInvoker` | 25.00% | FP32、整数插值 |
| RANSAC/基础矩阵相关函数，含下述 SVD | 8.84% | FP64 几何模型解算 |
| `cv::detail::ScharrDerivInvoker` | 7.55% | 整数梯度 |
| Eigen FP32 `gebp_kernel` 各变体 | 7.06% | FP32 后端矩阵乘法 |
| 直方图统计及 LUT 应用 | 7.01% | 整数图像处理 |
| `PyrDown` 各变体 | 5.90% | 整数金字塔 |
| Eigen FP32 矩阵打包函数 | 2.33% | FP32 数据加载/布局 |
| FAST | 2.15% | 整数角点检测 |
| 鱼眼去畸变函数及 `CamEqui` 包装自身 | 0.36% | FP64 内部计算、FP32 输入输出 |

RANSAC 行中，`cv::JacobiSVDImpl_<double>` **自身占 7.21%**，不能再与
8.84% 相加。1025 个 SVD 样本全部出现在 `TrackKLT::feed_stereo` 的时间跟踪
lambda 路径中。结合源码，`perform_matching()` 调用 `findFundamentalMat(...,
FM_RANSAC, ...)`，其 7 点模型用 7×9 的 `CV_64F` 矩阵进行 SVD，并求解三次多项式。
因此这是前端 RANSAC 的真实 FP64 热点，来自 OpenCV，而非 FP32 EKF 的矩阵乘法。

鱼眼去畸变连同捕获到的分配、数学函数等调用链约 **0.47%**；此数字与表内
自身周期的 0.36% 是不同口径。当前它没有表现为优先热点。
`FeatureInitializer` 三角化/几何优化的捕获调用链约 1.74%，主要在后端更新中调用。
后端 `do_feature_propagate_update` 捕获调用链约 27.88%，与阶段计时的比例相近。

## 验证与解释

采样回放完整结束，1970 帧、1782 帧初始化后输出，退出码 0。
与上一轮无 perf 的完整 FP32 加打包结果比较，时间戳、初始化、位姿以及逐帧
协方差维度/最小对角项/非对称程度字段全部相同；只排除 `compute_ms` 和本次关闭的
`cov_min_eigen` 字段。视觉处理均值为 76.72 ms，上一轮为 76.54 ms。
perf 的耗时不作为优化收益测量。

部署程序保留 ELF 符号表、删除 DWARF；本地完整调试程序与部署程序 `.text`
SHA256 相同，可以对照源码。符号映射通过本地 `symfs` 完成。

结论：前端确实存在值得单独评估的 FP64 **RANSAC SVD**。
若仅将这项耗时减半，按 7.21% 的采样占比估算，视觉处理时间约减少 3.6%，
不是整帧翻倍；这是基于周期占比的估计，尚无 FP32 RANSAC 实测结果。
更大的总体占比仍在 LK、Scharr、金字塔和直方图处理。
后续可分别评估 RANSAC 的模型求解/迭代次数及 SVD FP32 实验，并比较剔除结果与轨迹；
不能仅因输入点是 `Point2f` 就认定内部 SVD 已是 FP32。

## 复现

维护脚本 `prepare_camera_perf.py` 从隔离的回放程序生成独立 perf 适配器。
`run_camera_perf.sh` 使用 `--control=fifo:control,ack`，等待确认后开始计时。
本内核的 `PR_TASK_PERF_EVENTS_ENABLE/DISABLE` 遍历进程拥有的事件，无法启停
外部 perf 进程创建的事件，因此不用该方式。Linux 5.10 perf 的 ACK 写入含结尾 NUL，
适配器会跳过这个字节。

本次构建命令保存在 `install/openvins-backend-f32/perf-uzh/build.sh`，
只重新编译适配器，并链接既有完整 FP32 库。部署后在设备运行：

```sh
sh /mnt/data/vio-compare/backend-f32/run_camera_perf.sh \
  /mnt/data/vio-compare/backend-f32 0 200 perf-uzh-camera
```

程序配置为 `config-uzh-perf/estimator_config.yaml`；输出写入独立的 perf 和轨迹文件。
同名输出会覆盖，控制 FIFO 已存在时脚本拒绝启动。采样后取回数据，在本机分析：

```bash
/usr/lib/linux-tools-5.15.0-194/perf report \
  -i install/openvins-backend-f32/perf-uzh/perf-uzh-camera.data \
  --symfs install/openvins-backend-f32/perf-uzh/symfs \
  --stdio --no-children -g none --percent-limit 0
```

原始数据、调试程序、采样日志、完整轨迹、flat/callchain 报告、样本解析脚本及
`summary.json` 均保存于 `install/openvins-backend-f32/perf-uzh/`。
此次样本表示第 201–1970 帧视觉处理的周期分布，不表示整段墙钟时间或所有录像的热点。

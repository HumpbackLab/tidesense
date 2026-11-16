# TideSense 回环检测实验

独立入口可选地接入上游 ORB / DBoW2 场景识别、2D 几何验证和 OpenGV 3D 对齐。
这是检测实验，不运行位姿图优化，也不修改 Schur EKF、odom 或 IMU 预测位姿。
构建和运行默认关闭。实板结果见 [VALIDATION.md](VALIDATION.md)。
IVE 角点替换及 C906/TPU 匹配实验见 [offload/README.md](offload/README.md)。

## 构建

需要现有 SchurVINS 静态依赖和 AArch64 Ceres 静态库。
默认 Ceres 前缀复用 `install/vio-compare-20260924-121320/deps/ceres`，
可用 `SCHUR_LOOP_CERES` 指定其他兼容的目标库。
DBoW2、OpenGV 和词袋由 `prepare.sh` 按固定提交／SHA-256 准备。
`upstream.patch` 保存独立入口兼容改动；不依赖未记录的外部 checkout 修改。

```sh
SCHUR_WITH_LOOP_CLOSING=ON JOBS=4 scripts/schurvins/build.sh
# 原生 DDS 包使用单独的 *-native-dds-loop 目录，不覆盖普通构建。
# 只测算法时可加 SCHUR_ROS_BACKEND=none。
```

也可直接配置独立 CMake 工程，指定 `-DSCHUR_WITH_LOOP_CLOSING=ON`。
先运行 `prepare.sh`，并保留原有七项 NEON 优化参数。

## 运行

将实验包解压后，使用对应设备的实际标定。`run.sh` 自动选择包内词袋：

```sh
SCHUR_LOOP_CLOSING=1 SCHUR_PREDICTION=0 \
  SCHUR_LOOP_CSV=/tmp/loop.csv ./run.sh --vio schurvins.yaml auto /tmp/poses.csv 600
```

`SCHUR_LOOP_VOC` 可指定其他 DBoW2 ORB 词袋。实验默认使用上游提供的
`voc_VI01_8X4.yml.gz`；它不同于旧示例引用的 `voc_GEN_8X4.yml.gz`。
不能据本实验声称两个词袋的检出率相同。

可选标定文件配置：

```yaml
loop_closing:
  alpha: 0.8
  beta: 0.8
  frames_to_ignore: 20
  bow_thresh: 0.65
  min_num_3d: 10
  orb_dist_thresh: 48
  gv_2d_match_thresh: 0.10
  gv_3d_inlier_thresh: 0.4
  proximity_offset: 0.1
  proximity_dist_ratio: 0.01
  max_keyframes: 500
```

重叠阈值 `alpha` / `beta` 是 0..1 比例。默认采用上游双目示例中的 0.8，
不是旧 ROS 工厂缺参时的 40 / 20 回退值。
2D 验证采用 OpenCV essential-matrix RANSAC，3D 验证采用 OpenGV；
新增 ORB 像素通过与 VIO 相同的相机模型反投影，已有 VIO 点复用视线方向，
无需重采样整幅图像。

主线程复制图像和点数据形成不可变快照。单个工作线程处理检测，最多保留一个待处理输入，
队列已满时跳过新的回环输入并计数。VIO 输入仍采用现有 latest-pair 策略。
关键帧原图在处理后释放；历史描述子和几何信息保留。
达到 `max_keyframes` 会明确结束实验，不是自动裁剪或永久地图管理功能。

CSV 分别记录历史候选比较、词袋通过数、实际 2D 几何验证次数、
通过 2D / 地图点数量筛选的候选数和最终 3D 确认数。
结束日志包含 `received/submitted/processed/dropped/history`，不能只看 VIO 的 tracking 标志。

## 对照测量

设备上的脚本默认使用 `/mnt/data/schur-loop-closing`，日志写入 `/tmp`，
关闭 ROS 和 IMU 高频预测。不要同时运行其他相机程序。

```sh
./bench-board.sh off 0 600
./bench-board.sh on 1 600
# 相同录制输入的完整回放，不受相机 20 Hz 上限约束：
SCHUR_LOOP_REPLAY=/path/to/record ./bench-board.sh replay-off 0
SCHUR_LOOP_REPLAY=/path/to/record ./bench-board.sh replay-on 1
```

`SCHUR_LOOP_CALIBRATION` 可切换实验标定文件，`SCHUR_LOOP_APP` / `SCHUR_LOOP_BENCH`
可切换程序和输出目录。脚本采样进程 CPU、RSS、系统 CPU 和可用内存，
少于 16 MiB 可用内存时请求正常停止。原始结果取回主机后运行：

```sh
python3 scripts/schurvins/loop-closing/analyze.py RESULTS_DIR
```

实时 `processed_fps` 按图像时间跨度计算实际处理帧率，包含跳帧影响；
`steady_execution_fps` 按主机单调时钟计算，默认剔除前 50 帧。
回放应看 `completed_processing_fps`：包含读图、VIO、快照和最后的后台排空，
不含启动时词袋加载。回放的图像时间戳间隔不能当作计算速度。
单独的 `vio_wall_mean_ms` 不包含主线程快照复制和检测线程的全部开销。

`schurvins_loop_geometry_test` 在板端验证混合 ORB 像素 / VIO 视线的 2D 位姿恢复、
3D RANSAC 对齐和退化输入；分别传入鱼眼、radtan 标定运行。

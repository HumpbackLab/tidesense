# 本机验证记录（2026-09-18）

## 环境

- Rootless Podman 3.4.4，Ubuntu 20.04 / ROS Noetic 容器。
- 镜像：`localhost/tidesense-kalibr:noetic`。
- 最终镜像 ID：`0757a0e3be52fe8bbd3158f6cdec8bf6bc8a43207e43092373d2b5f7f8ea464d`。
- Kalibr：`1f60227442d25e36365ef5f72cd80b9666d73467`，37 个包全部编译成功。
- 6 个编译作业，纯编译 18 分 42 秒；转换库 `rosbags==0.9.23`、NumPy 1.24.4。
- 使用 rootless 默认映射、`--init`、Xvfb 和 Agg；运行容器禁用网络。

## 端到端命令

```bash
scripts/calibration/kalibr.sh /home/ncer/sophpi/cali/tidesense-stereo-calib \
  --output /home/ncer/sophpi/cali/tidesense-stereo-calib-kalibr-20260918
```

退出码 **0**，生成 ROS 1 bag、双目标定 YAML、文本摘要和 **11 页 PDF**。
PDF 已实际打开并检查首页。输出文件属于宿主机用户 ncer。

## 输入和转换验证

- 输入左右各 795 张，640×400、mono8，全部组成精确 header 时间戳对，无孤立图像。
- 采集时间跨度 158.800001 秒。
- 比较最终容器输出与原始 ROS 2 bag：1590 张图像的 SHA-256、尺寸、编码、step、
  frame_id 和 header 时间戳全部一致。原始目录只读挂载。
- 板端 header 时间位于 1970 年，主机记录时间位于 2026 年；转换仅把 ROS 1 bag
  索引时间设为原始 header 时间，不改图像时间戳，不改像素或图像方向。

## 最终标定结果

目标：A4 6×4 AprilGrid，tagSize 0.032 m，tagSpacing 0.3。
相机模型：两路均为 pinhole-radtan。

| 项目 | 结果 |
| --- | --- |
| 可检测标定板的图像 | 左 657 / 795，右 473 / 795 |
| Kalibr 处理的有效观测时刻 | 664 |
| 最终保留的观测时刻 | 101 |
| 剔除异常角点 | 231 |
| 基线长度 | 49.9714 mm |
| 相对旋转角 | 179.9642° |
| 左图二维重投影 RMS | 0.4662 px |
| 右图二维重投影 RMS | 0.3926 px |

二维 RMS 根据 Kalibr 输出的 x/y 残差均值和标准差计算：
`sqrt(mean_x² + mean_y² + std_x² + std_y²)`。旋转矩阵的正交性和行列式也已检查。
基线接近设计的 50 mm，旋转与右相机倒装约 180° 一致。

这些是本次数据上的拟合结果，不是独立测试集精度或实际打印尺寸的验证。
Kalibr 的初始化/选帧可能使重复运行结果略有变化。没有采集 IMU，因此不包含相机–IMU 外参。

结果目录：`cali/tidesense-stereo-calib-kalibr-20260918/`。
主要文件：`stereo-camchain.yaml`、`stereo-results-cam.txt`、`stereo-report-cam.pdf`。
构建/诊断日志位于忽略目录 `install/kalibr-podman/`。

## 已解决的环境问题

- Docker Hub 连接中断：改用 AWS ECR 的 Docker 官方 ROS 镜像副本；下载内容做 SHA-256 校验。
- Ubuntu / Python 下载慢：构建配置使用中科大 Ubuntu 镜像和清华 PyPI 镜像。
- `--userns=keep-id` 首次启动属主映射很慢：运行改用 rootless 默认映射，输出仍归当前用户。
- `xvfb-run` 作为 PID 1 等不到 Xvfb 就绪信号：加入 Podman `--init`。
- `igraph` 生成 PDF 时缺少 cairo：容器依赖加入 `python3-cairo`，完整复跑后报告生成成功。

日志里的 `rosdep view is empty` 不影响本流程：依赖已在构建阶段安装，离线标定无需 rosdep 更新。

## ROS 2 相机–IMU 入口验证（2026-09-22）

- 容器内 `test_convert.py` 的 7 个测试通过：图像像素、IMU 数值、header 与 bag
  时间保留；双目模式兼容；缺失 IMU、无时间重叠、重复 IMU 时间戳、null 噪声参数和
  分辨率不匹配均拒绝。
- 完整 `kalibr.sh --imu-cam --convert-only` 使用合成 ROS 2 bag 通过：输入左 21、
  右 20 帧，输出 20 对；全部 210 条 IMU 保留，平均 200 Hz、最大间隔 5 ms。
  测试故意使接收时间比采集时间晚 1000 秒，输出仍使用 header 时间。
- 非 convert-only 路径已进入 Kalibr 的图像提取和时间偏移初始化；合成图无标定板，
  因无有效角点而失败，外层脚本正确返回非零状态，没有输出成功提示。
  这不代表真实数据的相机–IMU 外参或报告已求解成功。
- 发现并修复旧容器 SciPy 1.3.3 与 NumPy 1.24 的 `typeDict` 不兼容，
  运行依赖升级为 SciPy 1.10.1；`build.sh --runtime-only` 上机验证通过。
  entrypoint 的 ROS 环境加载隔离了业务命令参数，避免误处理 `--help`。

合成噪声参数只用于软件测试，不得用于 SC7U22 实际标定。
测试产物位于忽略目录 `install/kalibr-podman/imucam-entry-test/`。

# TideSense VIO + TPU depth

共享一对 FSIN 同步的 640×400 原图：A53 运行 SchurVINS，TPU 同时把两张原图做 2×2 面积降采样到 320×200，再执行 D32、15×15 SAD 匹配。目标是每个相机周期（名义 20 Hz）输出位姿和深度。

```sh
# 主机编译；先准备 SchurVINS、micro-ROS 和 TPU SDK 依赖
scripts/vio-depth/build.sh
# 把生成的 tidesense-vio-depth-a53.tar 解包到板端 /mnt/data
SCHUR_MICRO_ROS_AGENT=192.168.123.100 /mnt/data/tidesense-vio-depth/run.sh
```

电脑端启动 Agent（另开终端运行下面的接收工具）：

```sh
source scripts/calibration/ros2-env.sh
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/scripts/schurvins/micro_ros/calibration-agent.xml"
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

micro-ROS Agent 使用现有 SchurVINS 的配置；`SCHUR_MICRO_ROS_PORT` 默认 8888，`SCHUR_MICRO_ROS_DOMAIN` 默认 0。此模式独占双目和 IMU，请先停止 VIO、depth、UVC、dataset 模式。

三个话题：`/tidesense/pose`（PoseStamped，odom 下的 IMU/body 位姿 T_W_B）、`/tidesense/depth/image_raw`（320×200，16UC1，毫米，0 无效）、`/tidesense/depth/camera_info`（矫正后虚拟相机内参）。同一对采集图像的三个消息使用完全相同的时间戳。位姿初始化前、IMU 未就绪、跟踪丢失或质量为 Insufficient 时只发布深度，不发布旧位姿。没有 200 Hz IMU 预测位姿输出。

深度参考相机由标定决定，当前为物理右相机经 180° 旋转和矫正后的虚拟光学相机。其 frame_id 为 depth_optical_frame；pose 的 body 原点与它不同，不能直接把 pose 当作深度相机的外参。降采样后的映射使用原图坐标 `(u-0.5)/2`，保留标定射线和基线的米制单位。

TPU 负责无符号平均池化、整型 SAD、多尺度窗口累加、最优视差、相邻视差代价和非相邻竞争代价。CPU 负责矫正、XSobel 预滤波、15% 唯一性与纹理过滤、Q4 亚像素插值、代价引导的反向投影一致性、100 像素散斑过滤和毫米转换。无效和搜索范围端点视差会被拒绝；这不是学习式深度模型，也不等同于完整左右双向匹配。

只有一次采集和一个待处理任务：先从该帧生成深度匹配输入；A53 运行 VIO 时，深度线程执行该帧的 TPU 匹配；随后主线程完成质量过滤，交给单独的 micro-ROS 线程。CPU 图像处理阶段按顺序调度，以减少单核竞争；原始 VIO 图像不被改写。处理过慢会选择较新的完整双目帧，日志明确统计跳帧；网络过慢会替换待发送的整组结果，不能保证任意场景/网络下固定 20 Hz。

```sh
# 板端几何、TPU 和质量过滤测试
/mnt/data/tidesense-vio-depth/bin/joint_depth_test
# 采集 400 帧并记录各阶段及跟踪状态
SCHUR_MICRO_ROS_AGENT=192.168.123.100 /mnt/data/tidesense-vio-depth/run.sh --frames 400 --profile /mnt/data/joint.csv
# 不连接 ROS 的处理性能诊断
/mnt/data/tidesense-vio-depth/run.sh --no-ros --frames 400 --profile /mnt/data/joint-no-ros.csv
```

`--help` 显示标定、传感器配置和 IMU 选项。CSV 记录同帧 sequence、左右 VI PTS、VIO 时间戳、ROS 时间戳、两分支耗时、线程 CPU 时间、跟踪状态及有效深度像素。VI PTS 是接收时间，现有标定的 live_camera_time_offset_ns 补偿后作为该同步采集对的统一时间；共享时间戳不表示独立测量了每张图的曝光中心。

此模式默认只在 VIO 图像金字塔的第 1、2 层提取 FAST 特征；VIO 仍接收原始 640×400 图像，特征坐标和内参仍在原分辨率定义。板端 perf 显示，在当前场景的第 0 层噪声上做 FAST 检测、打分和非极大值抑制占大量 CPU，跳过该提取层会改变特征分布，需另外验证运动精度。可用 `--detector-min-level 0` 对照原 VIO 配置；`--features`、`--seeds`、`--keyframes`、`--window` 可分别调整计算预算，实时配置默认为 60、90、3、3，FAST 阈值为 15。对照原 VIO 参数可用 `--detector-min-level 0 --fast-threshold 10 --features 120 --seeds 240 --keyframes 5 --window 4`。窗口和特征预算降低会改变定位行为，当前没有完成轨迹精度验证。

电脑端检查相同时间戳配对及接收频率：

```sh
source scripts/calibration/ros2-env.sh
python3 scripts/vio-depth/receive.py --seconds 30 --output /tmp/joint-result
```

接收工具按时间戳严格关联三个话题，输出 depth、pose、joined 的采集和接收频率。`--require-hz 19.5` 可要求实际配对接收频率；启动前或跟踪不足期间不会假造位姿来通过此检查。板测结果和限制见 [VALIDATION.md](VALIDATION.md)。

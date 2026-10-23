# TideSense 双目深度模式（micro-ROS / ROS 2 Humble）

独立于 SchurVINS 和 UVC 的板端深度模式。复用同步双目采集，A53 单线程
OpenCV StereoBM，发布无损数值深度；不运行 VIO。默认输出 320×200、请求 20 Hz，
双目原始采集仍为 640×400 / 20 Hz。可选择 640×400，但输出速率受计算和传输限制。

## 构建与部署

从 SDK 根目录执行，沿用已有 SchurVINS OpenCV、yaml-cpp 和 micro-ROS 静态库：

```sh
# 初次准备依赖；已有这些构建产物时可以跳过
SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh
scripts/depth/build.sh
```

生成 `install/soc_sg2002_tidesense_arm64_sd/tidesense-depth-a53.tar`。
复制到设备，在 `/mnt/data` 下解包。`build_tidesense_apps` / `build_all` 会自动构建并预装。

先停止 VIO、UVC、数据集录制等相机消费者。UVC 应通过其 `run.sh stop` 停止，
以恢复 IMU 200 Hz 和纯 RNDIS。采集程序沿用相机所有权锁；冲突会明确报错。
深度模式仅使用 IMU 的 FSIN 时基，不读取或发布 IMU 样本。

## 电脑端

Agent 使用现有 micro-ROS Humble 环境。在 SDK 根目录的第一个终端运行：

```sh
source scripts/calibration/ros2-env.sh
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/scripts/schurvins/micro_ros/calibration-agent.xml"
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

第二个终端接收并显示，依赖系统 Python 的 numpy、OpenCV 和 ROS 2 rclpy：

```sh
source scripts/calibration/ros2-env.sh
/usr/bin/python3 scripts/depth/receive.py --preview
# 或保存 30 秒的原始 16 位 PNG、相机参数和接收统计（目录必须不存在）
/usr/bin/python3 scripts/depth/receive.py --seconds 30 --output /tmp/tidesense-depth-run
```

`--max-metres 5` 仅控制预览颜色，不改变保存值。黑色为无效深度。
Agent 和订阅器沿用现有 Cyclone/本机 DDS 配置，细节见
[MICRO-ROS.md](../schurvins/MICRO-ROS.md)。

## 板端

在设备上执行，Agent 地址填电脑 USB 网卡地址：

```sh
cd /mnt/data/tidesense-depth
SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh
# 降低请求频率，减少 CPU 负载
SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh --fps 10
# 全分辨率，默认 D64；处理能力较低
SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh --width 640 --fps 4
```

Ctrl-C / SIGTERM 停止采集、关闭 FSIN 并退出。`--frames N` 限制选中处理的帧数，
不保证 ROS 交付帧数。`--help` 列出参数。`--calibration FILE` 接受 SchurVINS 双目标定，
支持 `radial-tangential` 或 `equidistant`；随包文件来自当前测试板，其他板卡必须替换。
`--disparities N` 必须是 16 的倍数，可调整测距下限与计算量。
通信默认启用，兼容现有 `SCHUR_MICRO_ROS_AGENT/PORT/DOMAIN` 环境变量；
`SCHUR_MICRO_ROS=0` 会报错，因为这个模式要求 ROS 输出。

## ROS 接口与坐标

| 话题 | 类型 | 内容 |
| --- | --- | --- |
| `/tidesense/depth/image_raw` | `sensor_msgs/msg/Image` | `16UC1`、小端、毫米、0 无效；是光轴 Z 距离，不是欧氏距离 |
| `/tidesense/depth/camera_info` | `sensor_msgs/msg/CameraInfo` | 校正后虚拟深度相机的内参，与图像的尺寸、时间戳及 frame_id 一致 |

节点 `/tidesense/tidesense_depth`。两个话题都是 reliable / volatile / depth=1；
micro-ROS 使用可靠 XRCE 分片发送大图，UDP MTU 沿用 1400、输出 history=32。
采集与 ROS 发送分别运行，输入和输出各最多保留一帧待处理图；网络拥塞会跳帧。
Agent 缺失或掉线时重试，重新连接后等待新深度图，不回放断线前的积压图。

图像已经去畸变、双目校正。CameraInfo 的 K/P 使用输出分辨率的校正内参，
D=0、R=I、P 的平移列为 0，描述单个虚拟深度相机，可按
`X=(u-cx)*Z/fx, Y=(v-cy)*Z/fy` 生成米制点云（先将 Z 从毫米转米）。
frame_id 为 `depth_optical_frame`（x 右、y 下、z 前）；当前标定以**物理右相机**为参考。
右图 180° 方向修正已折入 remap，不要在输入上再次旋转。
未发布与 IMU/body 的 TF，使用 RViz Image 直接显示，或将 RViz Fixed Frame
设为 `depth_optical_frame` 后查看该坐标系内生成的点云。

图像和 CameraInfo 使用参考相机原始 VI 到达时间，加启动时测得的
`CLOCK_REALTIME - CLOCK_MONOTONIC` 固定偏移。板端需先校准系统时间；
运行中避免跳变校时。没有套用 VIO 的相机–IMU 时间偏移，也没有将时间戳当作曝光中心。

## 算法与验证

沿用 [StereoBM 实验](../vio-compare/probes/stereo-bm/README.md) 的 block15、
XSobel cap31、texture10、uniqueness15、speckle100/range2、disp12MaxDiff1。
正视差基于投影矩阵选择参考相机，`Z_mm=fx*baseline_m*16000/disparity_q4`。
无效视差、校正图边界、匹配目标超界及超过 65.535 米的深度置 0。
默认启用经过输出对照验证的 `all64`（64 行分块、NEON widening、出窗差分重算），
仅替换深度程序链接的 StereoBM 实现，不改 SDK OpenCV 库。采集 helper 默认使用
缓存映射并在读取每张 VI DMA 图像前使旧缓存失效。
优化结果与复测方法见 [OPTIMIZATION.md](OPTIMIZATION.md)。

构建时设置 `DEPTH_STEREOBM_ALL64=OFF` 可回退标准匹配器；运行时
`SCHUR_CAMERA_CACHE=0` 可回退未缓存读取。诊断时可用
`SCHUR_CAMERA_VERIFY_CACHE=1` 对每张持有的 VI 图像逐像素比较缓存/未缓存读取，
诊断开销不应计入正常性能测量。

主机几何测试（无需 ROS）：

```sh
cmake -S scripts/depth -B install/tidesense-depth-host-tests \
  -DDEPTH_BUILD_APP=OFF -DDEPTH_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build install/tidesense-depth-host-tests -j4
ctest --test-dir install/tidesense-depth-host-tests --output-on-failure
```

测试覆盖已知视差的毫米换算、无效值、当前标定参考方向、
模拟右相机倒装的已知距离平面（两种分辨率）和无纹理图。
实际板端结果见 [VALIDATION.md](VALIDATION.md)。

perf 热点、20 Hz 请求下的实际吞吐量及复测步骤见
[PERFORMANCE.md](PERFORMANCE.md)。设置 `TIDESENSE_DEPTH_PROFILE=CSV_PATH` 可记录
校正、StereoBM、深度转换的单调时钟边界和主线程 CPU 时间；默认关闭。
CSV 写出位于计算阶段计时之外，采样期间建议写入 `/tmp`。

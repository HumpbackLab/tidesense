# TideSense 双目深度

板端通过 `tide_cam` 采集 640×400 同步双目图像，使用 OpenCV StereoBM 计算深度，并通过 micro-ROS 发布 ROS 2 图像与相机参数。默认输出 320×200，最高请求处理频率为 20 Hz。也可输出 640×400，实际速率取决于计算和网络负载。

该模式不运行 VIO，不读取 IMU 样本；IMU 仅提供相机 FSIN 时基。启动前需停止 VIO、录制器和 UVC 等相机消费者。停止 UVC 时应使用其 `run.sh stop`，以恢复 200 Hz IMU 频率。

## 构建与部署

需要项目使用的 OpenCV、yaml-cpp 和 micro-ROS 静态库。在 SDK 根目录运行：

```sh
SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh  # 首次准备依赖
scripts/depth/build.sh
```

生成 `install/soc_sg2002_tidesense_arm64_sd/tidesense-depth-a53.tar`。将包复制到设备并解压到 `/mnt/data`。构建脚本会链接 `tide_cam`，运行时不需要相机 helper。

## 运行

在电脑上启动 micro-ROS Agent：

```sh
source scripts/calibration/ros2-env.sh
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/scripts/schurvins/micro_ros/calibration-agent.xml"
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

在设备上指定电脑 USB 网卡的 IPv4 地址：

```sh
cd /mnt/data/tidesense-depth
SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh
```

`Ctrl-C` 或 SIGTERM 会停止采集并关闭 FSIN。常用参数：

| 参数 | 说明 |
| --- | --- |
| `--calibration FILE` | SchurVINS 格式的双目标定；默认使用包内 `calibration.yaml` |
| `--width 320` 或 `--width 640` | 深度图宽度；高度分别为 200 或 400 |
| `--disparities N` | 视差搜索范围，16 的倍数；默认按宽度选择 32 或 64 |
| `--fps N` | 输出选帧频率，0.1–20 Hz；不改变相机的 20 Hz 硬件触发 |
| `--frames N` | 处理 N 帧后退出；不保证 ROS 已交付相同数量 |

随包标定文件只适用于其对应设备。相机模型支持 `radial-tangential` 和 `equidistant`；其他设备需替换标定文件。曝光上限读取 `/mnt/data/tide-configurator/camera.conf`，也可用 `TIDE_CAM_CONFIG_PATH` 指定文件；文件不存在时使用 `tide_cam` 默认值。

## ROS 输出

| 话题 | 类型 | 内容 |
| --- | --- | --- |
| `/tidesense/depth/image_raw` | `sensor_msgs/msg/Image` | `16UC1`，小端、毫米，0 表示无效 |
| `/tidesense/depth/camera_info` | `sensor_msgs/msg/CameraInfo` | 校正后深度相机的内参与图像时间戳 |

图像经过双目校正。深度值表示光轴方向的 Z 距离，坐标系为 `depth_optical_frame`（x 右、y 下、z 前）；可用 `X=(u-cx)Z/fx`、`Y=(v-cy)Z/fy` 生成点云。相机到 IMU 的 TF 不在此模式中发布。

ROS 时间戳由参考相机的 VI 到达时间加启动时测得的系统时钟偏移得到。设备应先校准系统时间；此时间戳不是曝光中心，也未应用 VIO 的相机与 IMU 时间偏移。Agent 断开后会重连，只发送新的深度图。

在电脑上预览或保存深度图：

```sh
source scripts/calibration/ros2-env.sh
/usr/bin/python3 scripts/depth/receive.py --preview
/usr/bin/python3 scripts/depth/receive.py --seconds 30 --output /tmp/tidesense-depth-run
```

算法与历史性能数据见 [OPTIMIZATION.md](OPTIMIZATION.md)、[PERFORMANCE.md](PERFORMANCE.md)；验证记录见 [VALIDATION.md](VALIDATION.md)。

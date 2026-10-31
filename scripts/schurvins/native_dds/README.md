# TideSense 原生 DDS

板端以 `rcl + rclc + rmw_cyclonedds_cpp + Cyclone DDS` 直接发布标准 ROS 2 消息，电脑不需要 Agent。
VIO 的后端在编译时选择；本目录也保留独立的通信开销实验。

## 编译 VIO

```bash
SCHUR_ROS_BACKEND=native-dds scripts/schurvins/build.sh
# 对照或恢复默认：
SCHUR_ROS_BACKEND=micro-ros scripts/schurvins/build.sh
# 不包含 ROS 通信：
SCHUR_ROS_BACKEND=none scripts/schurvins/build.sh
```

主机需要 ROS 2 开发环境；构建脚本使用已加载的发行版，或通过 `TIDESENSE_ROS_SETUP` 指定安装。首次自动创建独立的构建 venv、获取锁定版本的依赖。
原生运行时可独立构建，不要求先编译 micro-ROS。源码版本锁定在本目录 `sources.repos`。
`SCHUR_WITH_MICRO_ROS=ON/OFF` 仍可用于原有构建脚本；设置 `SCHUR_ROS_BACKEND` 时以新开关为准。

输出：`install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-native-dds-a53.tar`。
包中含静态 VIO / 相机代码、ROS 运行库、匹配的动态加载器及许可证。
在设备 `/mnt/data` 解包后，用包内 `run.sh` 启动；无需修改系统库或运行库搜索路径。

## 运行

```bash
cd /mnt/data/schurvins-gcc11-native-dds
export SCHUR_ROS=1
export ROS_DOMAIN_ID=0
./run.sh --vio schurvins.yaml auto /mnt/data/vio-poses.csv 0
```

编译只决定后端，运行时默认仍关闭通信；`SCHUR_ROS=0` 不创建通信线程或 ROS 上下文。
原生后端只使用 `SCHUR_ROS`，不读取 micro-ROS 的 Agent / 端口配置。
`./run.sh --help` 显示 `ROS backend: native-dds`。

默认使用 Cyclone DDS 的自动发现，也可编辑包中 `cyclonedds.xml` 的板端网卡地址及电脑 peer，然后设置：

```bash
export CYCLONEDDS_URI="file://$PWD/cyclonedds.xml"
```

该文件是可选的固定 peer 示例，不会自动加载。电脑与板端配置相同 domain，并确保所选网卡和发现方式互通。
电脑可用 Fast DDS 或 Cyclone DDS 接收标准消息；在禁用组播的固定 peer 环境中需同步配置电脑发现参数。
例如电脑使用 Cyclone DDS、domain 0，网络发现配置好后执行：

```bash
ros2 topic echo /tidesense/pose geometry_msgs/msg/PoseStamped --qos-reliability best_effort
ros2 topic echo /tidesense/tracking std_msgs/msg/Bool --qos-reliability best_effort
```

消息沿用 `T_odom_body`、`odom` 坐标系、xyzw 四元数和原有采集时间戳映射；失去跟踪时仅发 tracking=false。
原生通信出错会在通信线程清理并重试，VIO 持续运行；队列总容量 64，过载丢弃最旧位姿。
标定继续通过原始数据录制和离线求解，不使用 ROS 图像录制通道。

SDK 整体构建可先 `export SCHUR_ROS_BACKEND=native-dds` 再执行 `build_tidesense_apps` / `build_all`。
镜像安装器按该后端选择 VIO 包并校验全部私有运行库；深度及联合深度应用仍使用自身的 micro-ROS 通信。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

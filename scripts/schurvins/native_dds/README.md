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

主机需要 ROS 2 Humble 开发环境，首次自动创建独立的构建 venv、获取锁定版本的依赖。
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

## 通信开销实验

对照组使用生产 `micro_ros_bridge.cpp` 及已有 `libmicroros.a`，另有通信关闭的基线。
以下是接入构建选项之前独立测试程序的测量方法。

## 比较方法

- SG2002 单核 A53，时钟由每次结果的 `board.txt` 记录。
- 同一生产线程按绝对期限提交 PoseStamped 与 tracking=true，两组通信线程均 nice=5。
- 两组均 best effort、64 条位姿 FIFO，队列满丢最旧；相同消息、时间戳、frame_id 与数值。
- 20 Hz 和 200 Hz；每次预热 5 秒后测量 30 秒，200 Hz 有订阅者的两组交换顺序各测两次。
- 测量整个发布进程 `CLOCK_PROCESS_CPUTIME_ID / CLOCK_MONOTONIC`，单核 100% 为满载。
  包含测试驱动、队列、序列化、发布与库内线程，排除电脑端 Agent / 订阅者 CPU，未单独计入板端中断上下文的网络开销。
- 另测 200 Hz 无下游订阅者：micro-ROS 的 Agent 保持在线；原生 DDS 没有匹配的数据订阅者。
- `receiver.py` 通过普通 ROS 2 订阅，核对测量窗口内的全部 600 / 6000 个位姿、消息内容、重复及接收间隔。
  tracking 消息不含时间戳，仅校验收到的值；不把接收间隔当作端到端延迟。
- RSS 取 `getrusage` 的进程峰值；`/proc/self/status` 同时保留线程数与内存字段。
  当前板端 `/proc` 的 VmRSS 对微型静态测试程序显示过低，结果不能将该字段当作准确内存用量。
- 不运行真实 VIO 或图像输出，不改动相机、IMU 或网页服务。结果衡量通信开销，不能直接等同于 VIO 集成后的总 CPU。

实测结果见 [验证记录](VALIDATION.md)。

## 复测

独立对照实验仍需主机 ROS 2 Humble、micro-ROS 构建缓存及主机 Cyclone DDS：

```bash
scripts/schurvins/native_dds/build.sh
scripts/schurvins/native_dds/build-bench.sh
source scripts/schurvins/native_dds/host-env.sh
read -rs -p 'Board password: ' TIDE_DEVICE_PASSWORD
export TIDE_DEVICE_PASSWORD
python3 scripts/schurvins/native_dds/run-board.py --seconds 30
unset TIDE_DEVICE_PASSWORD
```

`run-board.py` 需要 Paramiko，可使用 `tide_configurator/.venv/bin/python`。
当前测试网卡地址固定为电脑 `192.168.123.2`、板端 `192.168.123.99`，使用独立 domain 37、Agent UDP 18889。
DDS 使用固定 peer、禁用组播。测试程序不使用相机，可与现有网页预览并行。
运行器只关闭自身启动的 Agent / 订阅者，结束后删除自身上传的 `/mnt/data/tide-dds-bench-*` 目录。
不要在运行期间同时启动第二份同 domain 的测试。

构建源和库分别保存在 `install/native-dds/ws`、`install/native-dds/package`。
原生方案为动态库，micro-ROS 对照为现有静态库；均使用 SDK GCC 11.3 / Cortex-A53 Release 编译。
原生运行时使用同工具链配套的动态加载器和 libc，放在私有测试目录，不替换设备系统库。
具体源码提交记录在 `install/native-dds/sources.json`；底层 DDS 安全功能与共享内存关闭。
原始日志、库包、每轮 CPU 和接收统计保存在 `install/native-dds/results-*`。

架构参考：[rclc](https://github.com/ros2/rclc/tree/humble)、[ROS 2 Cyclone DDS RMW](https://github.com/ros2/rmw_cyclonedds/tree/1.3.5)。

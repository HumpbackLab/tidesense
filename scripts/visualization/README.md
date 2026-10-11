# ROS 2 VIO 轨迹显示

电脑端订阅位姿或里程计，生成 `nav_msgs/msg/Path` 并用 RViz2 显示。
只需要位姿消息，无需图像或 RTAB-Map。

## TideSense 位姿

先启动电脑端 micro-ROS Agent 和板端 VIO 位姿发布，命令见
[SchurVINS](../schurvins/README.md#实时-vio--micro-ros-位姿发布) 或
[OpenVINS](../vio-compare/openvins/README-live.md)。已有 Agent 时直接复用。
然后在电脑终端运行：

```bash
cd /home/ncer/sophpi
scripts/visualization/run.sh
```

默认订阅 `/tidesense/pose`（`geometry_msgs/msg/PoseStamped`），兼容 best-effort
和 reliable 发布者。窗口显示绿色轨迹及当前姿态的三轴；Fixed Frame 为 `odom`。
关闭窗口会停止本次电脑端桥接节点。

## 其他 Odometry 话题

```bash
scripts/visualization/run.sh --input-type odometry --input-topic /odom
```

输入类型为 `nav_msgs/msg/Odometry`。如果其 `header.frame_id` 不是 `odom`，在
RViz2 的 Global Options 中将 Fixed Frame 改成该名称。

## 输出与历史长度

- `/tidesense/path`：轨迹，最多 5 Hz 发布。
- `/tidesense/viz_pose`：当前位姿，最多 20 Hz 发布。
- TF：输入位姿的参考坐标系 → `tidesense_viz_body`，按可视化采样率发布。
  这个专用子坐标系用于显示输入位姿，不会发布 RTAB-Map 的 `map → odom` 修正。
- 默认最多保存 6000 个点，20 Hz 时约为最近 5 分钟；可以通过
  `--max-points 12000 --rate 20` 调整。位姿源停止后轨迹保留、当前位置冻结。
- 输入时间戳倒退或参考坐标系变化时自动开始新轨迹。VIO 在同一时钟和坐标系名下
  重置原点时，手动清空轨迹，避免把两个定位会话连接起来。

```bash
source scripts/calibration/ros2-env.sh
ros2 service call /tidesense/clear_path std_srvs/srv/Trigger '{}'
```

已有自己的 RViz2 配置时，可以只运行桥接节点，再添加 Path 和 Pose 显示：

```bash
source scripts/calibration/ros2-env.sh
/usr/bin/python3 scripts/visualization/vio_path.py
```

两个输出话题采用 reliable、transient-local QoS，RViz2 话题属性也应这样设置。
输入位姿使用其原始时间戳和坐标系；显示过程不修改估计结果，也不校正漂移。

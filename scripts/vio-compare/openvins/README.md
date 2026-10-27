# TideSense OpenVINS

本目录提供两种 OpenVINS 入口：SG2002 A53 上的实时双目惯性 VIO，以及 Linux 电脑上的无 ROS 数据集回放。实时程序使用 FP64 后端，可选 micro-ROS 位姿发布；部署和运行步骤见 [实时模式](README-live.md)。

## 实时模式

实时输入为两路 OV9281 640×400 图像和 SC7U22 200 Hz IMU，硬件 FSIN 约 20 Hz。程序使用独立相机辅助进程和 IIO 读取层，与 SchurVINS、录制器、UVC 和深度模式互斥。构建入口为：

```sh
JOBS=2 scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
python3 scripts/vio-compare/openvins/build_live.py
```

使用 [make_live_config.py](make_live_config.py) 将本设备的 SchurVINS 标定转换为 OpenVINS 配置；不要直接将 `schurvins.yaml` 传给 OpenVINS。完整部署命令、Agent 和 ROS 话题见 [README-live.md](README-live.md)。

## 离线回放

[run_record.cpp](run_record.cpp) 是无 ROS 的回放入口。它读取同一时钟域中的双目图像和 IMU，不按录制速度等待；参数依次为 OpenVINS 配置、数据集目录、轨迹 CSV 路径和最多处理帧数，`0` 表示全部：

```sh
run_record estimator_config.yaml /path/to/dataset trajectory.csv 0
```

数据集需要以下布局：

```text
dataset/
  imu.csv
  stereo.csv
  left/*.pgm
  right/*.pgm
```

`imu.csv` 每行为 `timestamp_ns,gx,gy,gz,ax,ay,az`，角速度单位 rad/s、加速度单位 m/s²；`stereo.csv` 每行为 `timestamp_ns,left_path,right_path`，图片路径相对数据集目录。时间戳为严格递增的纳秒整数，IMU 须覆盖相机区间。图片必须是 8 位 P5 PGM，尺寸为 640×400 或 640×480，左右一致。入口会将右图旋转 180°，因此应传入与该处理及标定相匹配的原始朝向图像。

独立录制器保存的 `frames.bin` 需先用 [数据集导出工具](../../dataset/README.md) 处理，再转换为上述 PGM 布局。输出 `trajectory.csv` 包含时间戳、初始化状态、位置、四元数、相机更新耗时及相机与 IMU 时间偏移。未初始化的行没有有效位姿；比较轨迹精度前应与真值对齐。

本仓库的 `scripts/vio-compare/build.sh openvins` 构建 A53 程序，不能在 x86 电脑直接运行。电脑端 `run_record` 需要在本机 OpenVINS 构建中编译，并安装相应的 OpenCV、Ceres、Eigen 和 Boost 依赖。相关源码版本与 A53 适配流程由 `scripts/vio-compare/` 管理。

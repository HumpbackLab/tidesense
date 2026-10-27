# TideSense 标定流程

标定复用通用原始录制器 `tidesense_record`，与数据集使用相同的图像、IMU、时间戳和
无损存储格式。板端无需运行 VIO、ROS、Agent 或单独的标定发布程序。

一次录制可以先求双目参数，再求相机–IMU 参数。图像覆盖和运动激励不足时需要补录，
不能仅用录制时长或优化退出成功判断标定质量。

## 标定板

打印 [A4 AprilGrid PDF](tidesense-aprilgrid-a4-6x4-32mm.pdf)，配套
[target YAML](tidesense-aprilgrid-a4-6x4-32mm.yaml)。PNG 仅用于预览。

- A4 横向，297×210 mm，打印选择实际大小 / 100%，关闭适合页面或缩放。
- tag36h11，6 列×4 行，ID 0–23；ID 0 在左下角，同一行向右递增，再向上一行。
- 标签外黑边边长 32 mm，间距 9.6 mm；`tagSize: 0.032`，`tagSpacing: 0.3`。
- 标签阵列外边缘 240×156.8 mm，包含外围角点黑块为 259.2×176 mm。
- 将图案贴在平整、坚硬的哑光板上，避免翘曲、反光或手遮挡。
- 打印后核对底部 100 mm 尺标和标签宽高。等比缩放时应重印或按实测尺寸修正
  `tagSize`；横纵比例不一致时重新制作。

标定与运行使用相同镜头焦点、图像方向、分辨率和 ISP 设置。
右相机倒装 180° 保留在真实外参中，采集程序不旋转图像。

## 板端录制

构建和部署见 [通用录制器说明](../dataset/README.md)。默认镜像预装于
`/mnt/data/tidesense-dataset`，以 root 在板端执行：

```sh
cd /mnt/data/tidesense-dataset
./run.sh --output /mnt/data/calib-motion --seconds 120
```

目录必须不存在，父目录需存在；Ctrl-C 也可提前正常停止。首次标定不用传
`--calibration`。相机、IMU 和 FSIN 需要由当前进程独占，先停止其他采集程序并关闭 PFC 预览。
SD 使用 async 挂载。等日志出现 `COMPLETE` 后再复制数据，异常停止的数据保留 `INCOMPLETE`。

默认采集双目 640×400 mono8、硬件触发约 20 Hz、SC7U22 200 Hz 原始 SI 数据。
图像时间为各自的 monotonic VI 到达时间，IMU 为 INT1 时间戳；不应用旧内参、
IMU 尺度校正或相机–IMU 时间补偿。硬件同步频率由触发源决定，没有录制 fps 设置。
`camera-config.yaml` 和 `imu-source.yaml` 保存采集参数，无需相机 INI。

`--width` / `--height` 可选择 640×480 或 1280×800；每种尺寸应有对应的真实标定。
本板 1280×800 长时间无损录制受压缩和 SD 吞吐限制，不能保证持续 20 Hz，队列满时会报错。
日常标定优先使用实际 VIO 运行的尺寸，并确认整个所需时段能完整录制。

## 如何运动

固定 AprilGrid，移动整个设备，保持两颗相机都有足够可识别的共同标签。
针对 A4 板与 640×400 图像，可以先从约 40–60 cm 开始，按实际清晰度、视场及标签像素调整。
这些距离是操作起点，不是镜头规格，也不能由 50 mm 基线直接推算。

1. 开始时静止几秒，观察两路图像是否清晰。
2. 缓慢改变位置、远近和板面倾角，让标签覆盖两路画面中央、边缘和四角。
3. 分别加入三轴平滑往返旋转和各方向平移，保持图案可检测；避免只绕单轴或只在平面内运动。
4. 结束前再静止几秒。使用明亮均匀照明，避免运动模糊、冲击及其他 AprilTag 干扰。

建议先录 60–120 秒，再通过角点覆盖、IMU 激励和求解报告判断是否充分。
相机内参阶段会从同一份录制取图，联合阶段保留连续 IMU，不需要再次启动专用采集程序。

## 电脑端导出

主机不需要 ROS。首次准备 Python 环境：

```sh
python3 -m venv .venv-dataset
.venv-dataset/bin/pip install PyYAML numpy zstandard rosbags==0.9.23
mkdir -p cali
scp -O -r root@192.168.123.99:/mnt/data/calib-motion cali/
.venv-dataset/bin/python scripts/dataset/export.py \
  cali/calib-motion cali/calib-export --time-offset-ns 0 --bag
```

导出目录必须不存在。程序核对压缩校验和、尺寸、连续帧序、IMU 时间间隔、首尾覆盖和完成标记，
输出无损 PNG、EuRoC 布局和 `dataset.bag`。`--time-offset-ns 0` 明确保留未补偿时间轴，
即使录制中保存过旧标定也不带入旧 shift。

ROS 1 bag 话题为 `/cam0/image_raw`、`/cam1/image_raw`、`/imu0`。
左右图像使用左 VI 时间作为同步帧组时间，各自原始 VI 时间另保留在 `frames.csv`。
消息和 bag 索引使用采集时间，不使用 USB 或网络接收时间。

## 双目标定

首次构建计算环境，见 [Podman / Kalibr 说明](kalibr/README.md)：

```sh
scripts/calibration/kalibr/build.sh
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag --output cali/cameras
```

默认目标为上述 A4 AprilGrid，模型为两路 `pinhole-radtan`。
鱼眼镜头可按实际模型选择：

```sh
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag \
  --lens fisheye --output cali/cameras-equi
```

只选择适合实际镜头的一种模型；切换模型需要重新估计，不能修改旧结果的模型名称冒充新标定。
输出包括 `stereo-camchain.yaml`、报告 PDF、残差摘要、输入校验和日志。
输入原始数据只读，结果目录拒绝覆盖。已有 ROS 2 bag 目录仍可使用相同入口。

## 相机–IMU 联合标定

使用同一份运动录制和刚生成的 camchain：

```sh
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag \
  --imu-cam --cam cali/cameras/stereo-camchain.yaml \
  --imu cali/sc7u22-imu.yaml --output cali/imucam
```

`sc7u22-imu.yaml` 需要已测定的采样率和四个噪声参数，其 `rostopic` 填 `/imu0`。
模板 [sc7u22-imu.yaml.example](sc7u22-imu.yaml.example) 中的 null 不能直接用于正式标定。
IMU 噪声参数应来自已有有效测量；短运动录制不能代替长时间静止噪声分析。
现有静止 ROS bag 可继续通过 `kalibr.sh ... --allan` 分析，ROS 1 文件需显式给出
`--imu-topic /imu0`。

联合模式默认使用 `scale-misalignment`，拟合原始 SI 输入的 IMU 内参、相机–IMU 外参和时间偏移，
默认固定陀螺加速度敏感项为零。脚本只支持 `scale-misalignment`，不再接受 `calibrated`。
相机话题、模型和分辨率从 camchain 读取，不再传 `--topics` / `--lens`。
相机内参和联合阶段必须使用相同图像方向、裁剪及尺寸。

## 将 Kalibr 结果用于 SchurVINS

联合标定成功后自动生成 `schurvins.yaml`。已有结果也可单独转换：

```sh
scripts/calibration/kalibr.sh --export-schurvins cali/imucam
```

默认输出文件已存在时拒绝覆盖，可用 `--output /path/to/new.yaml` 指定新文件。
转换支持当前三种采集尺寸，两颗相机必须尺寸相同；保留每颗相机各自的内参和畸变，
将 `T_cam_imu` 取逆得到 `T_B_C`。不要用设计的 50 mm 基线替换实测外参。

时间关系为 `t_imu = t_cam + shift`，只将 cam0 的 shift 应用一次到实时帧组时间，
保持 `delay_imu_cam: 0`。运行时在估计器入口对标称 SI 数据应用一套统一 IMU 内参；
不将某次标定 bag 的拟合零偏固化为长期启动偏置。

电脑端复制配置，然后板端启动：

```sh
scp -O cali/imucam/schurvins.yaml \
  root@192.168.123.99:/mnt/data/schurvins-gcc11-static-micro-ros/tidesense.yaml
# 以下在板端执行
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=0 ./run.sh --vio tidesense.yaml auto /tmp/vio-poses.csv 0
```

运行前检查 Kalibr 的角点覆盖、重投影残差、惯性残差、时间偏移和重复标定稳定性。
转换通过只说明参数满足运行入口要求，不能代替独立的 VIO 精度验证。

## 来源与重建

AprilGrid 编码和布局依据 [Kalibr 目标生成器](https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/aslam_offline_calibration/kalibr/python/kalibr_create_target_pdf)。
参考 [标定板说明](https://github.com/ethz-asl/kalibr/wiki/calibration-targets)、
[相机–IMU 标定](https://github.com/ethz-asl/kalibr/wiki/Camera-IMU-calibration)、
[IMU 噪声模型](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model)。
安装 PyMuPDF 后可执行 `python3 scripts/calibration/generate_aprilgrid_a4.py` 重建打印文件。

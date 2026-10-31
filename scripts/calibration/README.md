# TideSense 标定

使用通用录制器采集原始双目和 IMU，再在电脑运行 Kalibr；板端不需要 VIO 或 ROS。
一次充分激励的录制可先求双目参数，再求相机–IMU 参数。

## 打印与摆放

打印 [A4 AprilGrid](tidesense-aprilgrid-a4-6x4-32mm.pdf)，配套
[target YAML](tidesense-aprilgrid-a4-6x4-32mm.yaml)。A4 横向、实际大小 100%，关闭缩放。
图案为 tag36h11、6×4、32 mm 标签、9.6 mm 间距（`tagSize=0.032`、`tagSpacing=0.3`）。
贴在平整哑光硬板上，核对 100 mm 尺标；横纵缩放不一致需重印。

标定和运行保持相同焦点、分辨率、裁剪与图像方向。右相机倒装 180°，保留原图，
由外参表示安装方向。640×400 可先从约 40–60 cm 开始，按图案清晰度和双目共同视场调整。

## 录制（板端）

先停止其他相机/IMU 程序及网页预览，以 root 执行：

```sh
cd /mnt/data/tidesense-dataset
./run.sh --output /mnt/data/calib-motion --seconds 120
```

输出目录必须不存在、父目录存在，SD 使用 async 挂载。无需 `--calibration` 或相机 INI。
默认双目 640×400 / 20 Hz、IMU 200 Hz，保留原始 SI 与采集时间，不施加旧标定补偿。
`--width` / `--height` 可切换尺寸；1280×800 长时间无损录制可能超出吞吐，优先使用 VIO 尺寸。
等日志出现 `COMPLETE` 再取回数据，Ctrl-C 可正常停止，异常目录保留 `INCOMPLETE`。

固定标定板，移动整个设备：

1. 开始和结束静止几秒，确认双目清晰且均能看到标签。
2. 缓慢改变远近、位置与倾角，让角点覆盖中央、边缘和四角。
3. 加入三轴平滑往返旋转及平移，避免单轴运动、冲击和模糊。

建议录制 60–120 秒，使用明亮均匀照明；是否充分应根据覆盖、激励与求解报告判断。

## 导出（电脑，工程根目录）

```sh
python3 -m venv .venv-dataset
.venv-dataset/bin/pip install PyYAML numpy zstandard rosbags==0.9.23
mkdir -p cali
scp -O -r root@192.168.123.99:/mnt/data/calib-motion cali/
.venv-dataset/bin/python scripts/dataset/export.py \
  cali/calib-motion cali/calib-export --time-offset-ns 0 --bag
```

导出目录必须不存在。输出 PNG、EuRoC 目录及 `dataset.bag`，话题为
`/cam0/image_raw`、`/cam1/image_raw`、`/imu0`。
`--time-offset-ns 0` 保留未补偿时间轴；左右 bag 图像取左 VI 帧组时间，各路原时间保留在 CSV。

## 双目与相机–IMU 标定

```sh
scripts/calibration/kalibr/build.sh
# 普通镜头默认 pinhole-radtan；鱼眼使用 --lens fisheye（pinhole-equi）
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag \
  --lens fisheye --output cali/cameras
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag \
  --imu-cam --cam cali/cameras/stereo-camchain.yaml \
  --imu cali/sc7u22-imu.yaml --output cali/imucam
```

选择适合镜头的模型，不要仅修改旧 YAML 的模型名。联合模式从 camchain 读取相机话题、
模型和尺寸，不再传 `--lens` / `--topics`；前后阶段的图像方向与尺寸必须一致。
联合模式使用 `scale-misalignment`，默认估计时差，默认固定陀螺加速度敏感项为零。
IMU YAML 需实际采样率及四个噪声参数，[模板](sc7u22-imu.yaml.example) 的 null 不可直接使用。

静止噪声数据可单独分析；ROS 1 bag 显式指定 IMU 话题：

```sh
scripts/calibration/kalibr.sh cali/sc7u22-static --allan
scripts/calibration/kalibr.sh cali/static.bag --allan --imu-topic /imu0
```

## 应用结果

联合求解成功后自动生成 `schurvins.yaml`，已有结果也可单独转换：

```sh
scripts/calibration/kalibr.sh --export-schurvins cali/imucam
scp -O cali/imucam/schurvins.yaml \
  root@192.168.123.99:/mnt/data/schurvins-gcc11-static-micro-ros/tidesense.yaml
# 以下在板端执行
cd /mnt/data/schurvins-gcc11-static-micro-ros
SCHUR_MICRO_ROS=0 ./run.sh --vio tidesense.yaml auto /tmp/vio-poses.csv 0
```

转换保留各相机内参、将 `T_cam_imu` 取逆为 `T_B_C`，不以设计基线替代实测外参。
运行时应用一次统一 IMU 内参、一次 cam0 时间偏移，保持 `delay_imu_cam: 0`；残余零偏由 VIO 估计。
检查角点覆盖、重投影/惯性残差、时间偏移及重复标定稳定性，求解成功不代表精度合格。
输出目录/默认文件拒绝覆盖；工具选项见 [Kalibr](kalibr/README.md)，录制格式见
[数据集工具](../dataset/README.md)。安装 PyMuPDF 后可用 `generate_aprilgrid_a4.py` 重建打印文件。

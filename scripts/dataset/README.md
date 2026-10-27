# TideSense 数据集录制

`tidesense_record` 在设备上独立录制同步双目图像和 SC7U22 IMU，不运行 VIO，也不依赖 SchurVINS 相机进程。图像由 `tide_cam` 在同一进程采集并无损压缩；保留左右 VI 到达时间戳和 IMU 中断时间戳。采集模式为 640×400（默认）、640×480 和 1280×800 mono8，硬件同步约 20 Hz、IMU 200 Hz。1280×800 对写盘带宽要求较高，当前板卡仅验证了短时录制；持续录制应使用较低分辨率。

录制的原始数据可用于相机与 IMU 标定、SchurVINS 回放，或导出为 EuRoC 风格目录及 ROS 1 bag。TideConfigurator 提供网页预览、录制和取回流程。

## 构建与部署

`build_all` 会构建并预装录制器。单独构建时，在 SDK 根目录运行：

```sh
scripts/dataset/build.sh
```

生成 `install/soc_sg2002_tidesense_arm64_sd/tidesense-dataset-a53.tar`，在设备 `/mnt/data` 解包。首次构建需先准备 `scripts/schurvins/build.sh` 生成的 GCC 11、OpenCV 和 yaml-cpp 依赖；录制器本身不链接 VIO。

## 录制

启动前停止其他相机或 IMU 消费者，确认数据分区有足够空间。在设备上运行：

```sh
cd /mnt/data/tidesense-dataset
./run.sh --output /mnt/data/dataset-001
```

输出目录必须不存在。Ctrl-C 正常停止，排空写入队列并保存完成标记。常用参数：

| 参数 | 作用 |
| --- | --- |
| `--seconds 120` | 到时自动停止；省略时手动停止 |
| `--width 640 --height 480` | 选择受支持的采集尺寸 |
| `--queue-pairs N` | 调整图像与压缩包的总待写容量，范围 1–128 |
| `--calibration SCHUR.yaml` | 随录制保存标定；首次标定可省略 |

相机曝光上限可通过设备 `/mnt/data/tide-configurator/camera.conf` 调整，修改后重启采集生效。原始图像不旋转、不缩放、不去畸变；IMU 保存标称 SI 单位，不预先应用 VIO 的 IMU 内参。

## 文件与完整性

主要输出包括 `frames.bin`、`imu.csv`、`camera-config.yaml`、`imu-source.yaml` 和 `metadata.yaml`。每对双目图像经逐行 uint8 差分和 Zstandard 无损压缩后写入 `frames.bin`，附带校验和。压缩与写盘在独立线程执行；跟不上采集时会报错并留下 `INCOMPLETE`，不会静默丢帧。

`COMPLETE` 表示录制已结束并完成刷盘；它不代表整段存储文件已从 SD 卡重新读取。取回电脑或开始标定时仍需校验压缩帧、图像尺寸、数量和相机与 IMU 连续性。录制器保存的图像时间是 VI 到达时间，不是曝光中心；算法回放应使用匹配该设备的时空标定。

`frames.bin` 的文件头为 `TIDESENSE_ZST_V1\n`。之后每段包含 4 字节小端压缩长度及一个独立的 Zstandard 帧；解压后是 56 字节 CameraHeader，紧接左右各 `width × height` 字节的差分图像。格式和校验实现见 [export.py](export.py)。

## 导出

在电脑上安装导出依赖，并将录制目录取回本地：

```sh
python3 -m venv .venv-dataset
.venv-dataset/bin/pip install PyYAML numpy zstandard rosbags==0.9.23
.venv-dataset/bin/python scripts/dataset/export.py dataset-001 dataset-export --bag
```

导出结果包括 `mav0/cam{0,1}/data/` 与 `mav0/imu0/data.csv`、供 SchurVINS 使用的 `stereo.csv` 和 `imu.csv`，以及可选的 `dataset.bag`。有匹配标定时生成 `replay.yaml`；没有标定仍可导出图像和 IMU。右图保持原始方向，其装配朝向由真实外参表达。

`dataset.bag` 是 ROS 1 bag，话题为 `/cam0/image_raw`、`/cam1/image_raw` 和 `/imu0`。使用 OpenVINS 或其他算法时，应提供本设备的内参、畸变、外参、IMU 噪声和时间偏移。历史验证记录见 [VALIDATION.md](VALIDATION.md)。

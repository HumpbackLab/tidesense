# TideSense 标定

从一份 `tidesense_record` 原始录制完成双目和相机–IMU 标定，生成 SchurVINS 配置。
入口、公共 Python 模块、标定板、依赖清单和容器构建文件都位于本目录。
本工具不调用或导入 `scripts/` 下的脚本，目录可以整体复制到其他 Linux 电脑使用。

## 准备环境

在工程根目录执行：

```sh
python3 -m venv calibration/.venv
calibration/.venv/bin/pip install -r calibration/requirements.txt
calibration/environment/build.sh
```

需要 Python 3.8 或更高版本、rootless Podman、catatonit。首次完整构建还需 curl、tar、
sha256sum 和联网环境；主机不需要安装 ROS。以普通用户运行，不要 sudo。
默认镜像为 `localhost/tidesense-calibration:noetic`，`KALIBR_IMAGE` 可指定其他镜像标签。

已有固定版本 Kalibr 镜像时，可以复用其编译产物来构建新镜像：

```sh
calibration/environment/build.sh --base localhost/tidesense-kalibr:noetic
```

这个选项仅是构建加速，完整构建不需要旧镜像或旧脚本。基础镜像需包含配套 Kalibr 源码和
运行依赖；构建时核对源码 SHA-256，构建后检查求解器依赖。已有镜像保持原样。
Kalibr 固定为 `1f60227442d25e36365ef5f72cd80b9666d73467`，下载归档带 SHA-256 校验。
完整构建缓存默认位于 `install/calibration-environment/`，可通过 `KALIBR_BUILD_DIR` 指定。

IMU 固定使用 `scale-misalignment`。陀螺加速度敏感矩阵 A 在容器构建时固定为零，
运行时检查配套标记及模块校验和，不再读取、修改或挂载覆盖 Kalibr Python 源码。

## 录制

打印 [A4 AprilGrid](targets/aprilgrid.pdf)，选择实际大小 / 100%，关闭适合页面和缩放。
配套 [target YAML](targets/aprilgrid.yaml)：tag36h11、6 列×4 行、黑色标签边长 32 mm，
标签间距为边长的 0.3 倍（9.6 mm）。打印后测量标签边长和 PDF 中的尺标，贴在平整硬板上。
默认标定和 VIO 使用相同的焦点、图像方向、分辨率及 ISP 设置，采集不旋转右相机图像。

板端使用已部署的通用录制器：

```sh
cd /mnt/data/tidesense-dataset
./run.sh --output /mnt/data/calib-motion --seconds 120
```

默认双目 640×400、硬件同步约 20 Hz，IMU 200 Hz；首次录制不需要已有标定参数。
停止其他相机/IMU 采集程序，保留录制目录中的全部文件，等待 `COMPLETE` 后复制到电脑。

固定标定板，移动整个设备。先缓慢改变距离、位置和倾角，覆盖两路画面的中央、边缘和四角；
再加入三轴平滑往返旋转及各方向平移。保持标签可检测、照明充分，避免模糊和冲击。
60–120 秒可作为开始尝试的时长，最终以覆盖、运动激励和求解报告判断是否充分。

## 一条命令完成标定

准备实测 IMU 噪声配置，参考 [imu-noise.yaml.example](imu-noise.yaml.example)。
必须填写采样率以及四个正数噪声参数；`null` 模板会被拒绝。短运动录制不能替代静止噪声测量。
工具自动设置话题 `/imu0`，原始噪声文件和实际输入配置都会归档。

```sh
calibration/.venv/bin/python calibration/calibrate.py cali/calib-motion \
  --imu cali/sc7u22-imu.yaml --lens wide --output cali/calib-result
```

| 镜头参数 | Kalibr 模型 | SchurVINS 畸变模型 |
| --- | --- | --- |
| `--lens wide`（默认，广角） | `pinhole-radtan` | `radial-tangential` |
| `--lens fisheye`（鱼眼） | `pinhole-equi` | `equidistant` |

两颗相机使用同一种镜头模式，各自估计内参和畸变。更换模式需要重新求解。
`--target /path/to/target.yaml` 可指定实际 AprilGrid；省略 `--output` 会在录制目录旁新建带时间戳的目录。

程序通过本目录内的 Python 模块直接调用两次容器求解器，两次读取同一个 `dataset.bag`，
没有 shell 调度层或中间 bag 转换，也不展开 PNG。bag 在求解器容器中单独只读挂载。
像素、方向、IMU 原始 SI 值保持不变，左右消息共用左相机 VI 帧组时间。
不读取旧标定快照、不应用旧时间补偿；每路原始 VI 时间仍保留在输入录制中。

结果目录必须不存在，父目录需存在。失败立即停止并保留日志及 `INCOMPLETE`，不支持断点继续。
所有阶段和格式导出完成后才生成 `COMPLETE`。求解完成不等于质量合格，结果标记为 `requires_review`。

## 结果

```text
calib-result/
  dataset.bag                   两阶段共用的唯一 bag
  dataset-camchain.yaml          双目内参和外参
  dataset-camchain-imucam.yaml   相机–IMU 外参和时间偏移
  dataset-imu.yaml               IMU 尺度/轴不对齐参数
  dataset-report-cam.pdf         双目报告
  dataset-report-imucam.pdf      相机–IMU 报告
  dataset-results-*.txt          原始残差摘要
  schurvins.yaml                 最终运行配置
  report.md                     基线、采样率、时间偏移和物理残差摘要
  calibration.json              执行状态、输入/代码校验和、结构化结果
  cameras.log / imucam.log       两次求解日志
  *-command.json                求解命令及实际使用的镜像 ID
  COMPLETE                      全部阶段完成标记
```

检查报告中的角点覆盖和残差分布、时间偏移、重复标定稳定性，再用独立 VIO 数据验证。
设计的 50 mm 基线只用于核对，不替换实测结果。支持现有三种采集尺寸，左右尺寸必须一致，
每种运行尺寸需有匹配的真实标定。

`schurvins.yaml` 使用 `T_B_C = inverse(T_cam_imu)`，时间关系为 `t_imu = t_cam + shift`。
仅将 cam0 的 shift 应用一次到实时帧组，保持 `delay_imu_cam: 0`。
运行时对标称 SI 数据应用一次完整 IMU 逆模型，单次标定的拟合零偏不固化为长期启动偏置。

## 目录与验证

```text
calibrate.py            命令行入口
tide_calibration/
  pipeline.py           流程编排和输入配置
  recording.py          原始录制校验、解码及 bag 写入
  kalibr.py             容器预检和两阶段求解
  schurvins.py          坐标、时间及 IMU 逆模型转换
  report.py             结果汇总
environment/            独立容器构建文件
targets/                标定板 PDF 和 YAML
tests/                  数据往返、失败路径、移植及数学转换测试
```

```sh
cd calibration
.venv/bin/python -m unittest discover -s tests -v
```

测试的合成噪声和求解结果仅用于软件验证。真实容器与入口验证边界见 [VALIDATION.md](VALIDATION.md)。

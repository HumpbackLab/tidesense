# 在本机用 OpenVINS 回放双目／IMU 数据集

设备实时运行、FP64 后端/150 点及 ROS 2 位姿发布见 [README-live.md](README-live.md)。

本目录的 [`run_record.cpp`](run_record.cpp) 是 OpenVINS 的无 ROS 离线入口：读取双目灰度图和 IMU，逐帧输出位姿与处理耗时。下面的命令在 **x86 Linux 本机**执行，不需要连接 TideSense。`scripts/vio-compare/build.sh openvins` 构建的是 **SG2002 A53** 程序，不能直接在本机运行。

SDK 的新构建默认使用 **16×15 LK 窗口**，由 `sync.sh` 中的
[`openvins-lk-window.patch`](../patches/openvins-lk-window.patch) 接入。
设置 `OPENVINS_LK_WINDOW_16X15=OFF scripts/vio-compare/build.sh openvins`
并重建可恢复 15×15。已有二进制不会随源码配置变化而自动更新。

## 本机快速运行

从 SDK 根目录执行。这里的 `run_record` 是本机已有的 OpenVINS 构建产物；若换一台电脑或清理了 `install/`，先看下文“重新构建”。

```bash
cd /home/ncer/sophpi

runner="$PWD/install/dataset-analysis-20260924-155813/pc/openvins/build/run_record"
config="$PWD/install/vio-compare-20260924-121320/openvins/config/estimator_config.yaml"
dataset="$PWD/install/dataset-analysis-20260924-155813/replay" # 换成自己的数据集目录
result="$PWD/install/openvins-local-result"

mkdir -p "$result"
OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 \
  "$runner" "$config" "$dataset" "$result/trajectory.csv" 0 \
  >"$result/run.log" 2>&1
echo "exit code: $?"
tail -n 5 "$result/run.log"
```

命令参数依次是 `estimator_config.yaml`、数据集目录、输出 CSV 路径、最多处理的双目帧数。最后一个参数为 `0` 时处理全部帧；调试时可用 `100` 先跑前 100 帧。运行时不会按录制帧率等待，处理每一帧后继续读下一帧。`run.log` 末尾的 `DONE frames=... initialized=... avg_ms=... wall_s=... peak_rss_kb=...` 可用于确认处理数量和耗时；`trajectory.csv` 是主要结果。

## 数据集输入格式

当前入口要求如下布局；`stereo.csv` 中的图片路径**相对数据集目录**：

```text
my-dataset/
├── imu.csv
├── stereo.csv
├── left/
│   └── 1000000000.pgm
└── right/
    └── 1000000000.pgm
```

`imu.csv` 每行是 `timestamp_ns,gx,gy,gz,ax,ay,az`，角速度单位 rad/s，加速度单位 m/s²。`stereo.csv` 每行是 `timestamp_ns,left_path,right_path`，左右图共用这一帧时间戳。例如：

```text
# imu.csv
#timestamp_ns,gx,gy,gz,ax,ay,az
995000000,0.01,-0.02,0.00,0.1,0.0,9.8
1000000000,0.01,-0.02,0.00,0.1,0.0,9.8

# stereo.csv
#timestamp_ns,left_image,right_image
1000000000,left/1000000000.pgm,right/1000000000.pgm
```

时间戳必须是同一时钟域的**纳秒整数**，按时间递增；IMU 要覆盖全部相机时刻，并在首帧前留足静止初始化数据。CSV 可以有以 `#` 开头的注释行，不能使用普通文字表头。入口只读取 IMU 前七列，后续列会忽略。

图像必须是 **P5（二进制）PGM、640×400 或 640×480、8 位单通道、最大值 255**，左右图尺寸必须一致；当前读图函数不支持 PNG/JPEG、其他尺寸，也不支持带 PGM 注释的头部。程序会把**右图自动旋转 180°**，左图保持原样，因此 TideSense 数据应传入原始朝向的左右图。对本来已经转正的右图，可在导出 PGM 时预先旋转 180°，使入口旋转后恢复原图；相机标定必须与实际送入 OpenVINS 的图像一致。其他图像格式需要修改 [`run_record.cpp`](run_record.cpp) 的读图逻辑；仅改文件后缀不够。

TideSense 独立录制器的原始数据为 `frames.bin`，不能直接交给这个入口。参见 [`scripts/dataset/README.md`](../../dataset/README.md) 使用 `scripts/dataset/export.py` 导出；该导出器生成 PNG，因此还需转换为上述 PGM，并把 `stereo.csv` 的图片路径改为 PGM 路径。另一个可直接查看的输入示例是 `install/dataset-analysis-20260924-155813/replay/`。

## 标定与时间对齐

传给第一个参数的 `estimator_config.yaml` 通过 `relative_config_imu`、`relative_config_imucam` 引用同目录下的 `kalibr_imu_chain.yaml`、`kalibr_imucam_chain.yaml`，三份文件要一起保存。现有 TideSense 配置位于 `install/vio-compare-20260924-121320/openvins/config/`，可作为本板数据的起点。用自己的传感器时必须替换真实的相机内参、畸变、相机与 IMU 外参、IMU 噪声，以及对应的时延；不能直接套用 TideSense 参数。

如果已有 TideSense 的 Kalibr `imucam-camchain-imucam.yaml` 和 `imucam-imu.yaml`，可从 SDK 根目录重新生成配置：

```bash
python3 install/vio-compare-20260924-121320/openvins/make_config.py \
  --kalibr-dir /path/to/kalibr-result \
  --output-dir /path/to/openvins-config
```

此脚本需要 PyYAML，并假定先运行过 `scripts/vio-compare/sync.sh`，把脚本和 OpenVINS 上游源码同步到同一个 `install/vio-compare-20260924-121320/openvins/` 目录。它**专用于 640×400 TideSense**：把右图旋转对应的虚拟相机参数写入标定，将 `timeshift_cam_imu` 置零。上述示例数据的相机时间戳已加过标定的相机／IMU 时间偏移，回放时不能再加一次；其他数据集应按自己的时间戳处理方式配置。新 TideSense 录制数据保存标称 SI，回放须使用匹配的完整 IMU 内参；历史录像需按其输入域迁移。

## 读取结果

`trajectory.csv` 每个相机帧一行：

```text
timestamp_ns,initialized,px,py,pz,qx,qy,qz,qw,compute_ms,cam_imu_timeoffset_s
```

`initialized=0` 表示尚未初始化，该行位姿列为空；`initialized=1` 时 `p*` 是 OpenVINS 世界系中的 IMU 位置（米），`q*` 是世界系到 IMU 系的四元数 `q_GtoI`，顺序为 `x,y,z,w`。`compute_ms` 是该帧 `feed_measurement_camera()` 的耗时，**不包括**图像读取、右图旋转、外部 IMU 输入和 CSV 写出。`cam_imu_timeoffset_s` 为该帧滤波器使用的相机到 IMU 时间偏移，遵循 `t_imu = t_cam + offset`；固定标定时各帧相同，在线估计时可用于检查收敛。世界系由初始化建立，结果不自动对齐地图或真值。首末位置差只有在设备确实回到起点时才可作为粗略漂移参考；正式精度比较需要真值轨迹和坐标对齐。

可以快速检查有多少帧得到位姿，并查看末帧：

```bash
awk -F, 'NR>1 && $2==1 {n++; last=$0} END {print "initialized frames:", n; print "last pose:", last}' "$result/trajectory.csv"
```

如果 `initialized frames` 为 0，先核对 IMU 覆盖、时间偏移、相机朝向、双目外参与图像质量。进程退出码为 0 只表示回放执行完成，不表示轨迹可信。

## 重新构建本机程序

本仓库受版本管理的 `scripts/vio-compare/openvins/` 主要维护 A53 适配；上面的 x86 可执行文件及其 OpenCV/Ceres 构建树保存在 Git 忽略的 `install/dataset-analysis-20260924-155813/pc/`，不会随仓库克隆带走。本机已有源码和依赖时，可用下列命令重建现有 x86 工作区：

```bash
cd /home/ncer/sophpi
pc="$PWD/install/dataset-analysis-20260924-155813/pc"
cmake -S "$pc/openvins/open_vins/ov_msckf" -B "$pc/openvins/build" \
  -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_ROS=OFF -DENABLE_ARUCO_TAGS=OFF \
  -DOpenCV_DIR="$pc/stage/share/OpenCV" \
  -DCeres_DIR=/home/ncer/fpga_vio/.tools/ceres-1.14/lib/cmake/Ceres
cmake --build "$pc/openvins/build" --target run_record -j4
```

这套本机工作区使用 GCC 11、OpenCV 3.4.20、Boost 1.74、Eigen 3.4、Ceres 1.14。若要在**另一台机器从零构建**，需要自行安装这些本机依赖，并把受版本管理的 `run_record.cpp` 接入 OpenVINS `ov_msckf` 的 CMake；现有 `scripts/vio-compare/build.sh openvins` 不负责 x86 构建。

## 默认 LK 过滤

A53 构建默认打开 `OPENVINS_LK_FILTER_VALID=ON`：RANSAC 只使用 LK 成功的匹配点。关闭方法：

```sh
OPENVINS_LK_FILTER_VALID=OFF scripts/vio-compare/build.sh openvins
```

IMU 初值和浅层/回退实验已撤销。实现与验证见 [LK 过滤说明](../probes/lk-filter/README.md)。

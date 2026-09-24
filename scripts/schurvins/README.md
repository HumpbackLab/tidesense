# TideSense / Cortex-A53 SchurVINS

现已支持 OV9281 **实时双目图像输入与纯视觉前端**，用法和实测见 [CAMERA.md](CAMERA.md)。现已接入 SC7U22 **200Hz 实时 IMU**，双目＋惯性入口见 [IMU.md](IMU.md)；原离线双目＋IMU 入口保留。

可选 **micro-ROS 位姿/跟踪状态发布**见 [MICRO-ROS.md](MICRO-ROS.md)。构建开关
`SCHUR_WITH_MICRO_ROS=ON`；运行时还需显式设置 `SCHUR_MICRO_ROS=1` 和电脑端
`SCHUR_MICRO_ROS_AGENT`。两层开关默认均关闭，`SCHUR_MICRO_ROS=0` 可独立运行 VIO。

独立双目标定采集：`--calibrate SENSOR.ini --fps 5`，直接发布 ROS 图像，见 [CALIBRATION.md](CALIBRATION.md)。

源码：SDK 根目录 `SchurVINS/`，来自 <https://github.com/bytedance/SchurVINS>。
精确版本与依赖见 `sources.lock`；此目录保存 SDK 构建入口、运行程序和上游补丁。
`SchurVINS/` 与 SDK 其它独立源码库一样不直接纳入主库，构建脚本可恢复指定版本并应用补丁。

## GCC 11 全静态版本

```sh
JOBS=4 ./scripts/schurvins/build.sh
```

使用 SDK 的 Arm GNU Toolchain 11.3.Rel1（GCC 11.3.1），为 Cortex-A53 重编
SchurVINS、OpenCV、yaml-cpp、glog 0.4.0 和 FAST；Eigen、minkindr、Boost 使用头文件。
最终通过 `-static` 链入工具链的 glibc、libstdc++ 和 libgcc。
新增实时采集辅助进程 `schurvins_camera` 使用板上现有 libc，主程序仍为全静态，详见 `CAMERA.md`。
关闭 OpenCV ITT 的动态插件加载，以及 glog 的 NSS 用户名查询（日志用户名取 `USER`，
未设置时使用其内置占位名），避免这两项功能引入运行时动态库需求。
构建脚本检查 ELF 不含 `PT_INTERP` 或 `DT_NEEDED`。

构建缓存：`install/schurvins-gcc11-static/`，源码缓存位于 `install/schurvins-build/deps/`。
输出目录：`install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static/`。
安装包：同级 `schurvins-gcc11-static-a53.tar`（不压缩，板端无须 gzip）。
传到板端后使用 `tar -xf schurvins-gcc11-static-a53.tar -C /mnt/data` 解包。
该版本无须部署 OpenCV 动态库，也无须升级板上 glibc。
`BUILD-INFO` 记录编译器与构建模式；全静态程序可直接执行：

```sh
./bin/schurvins_stereo --self-test synthetic.yaml
./bin/schurvins_stereo actual-calibration.yaml stereo.csv imu.csv poses.csv
```

已在当前板上验证（2026-09-17）：在不含系统动态库的应用目录内，以 `env -i` + `chroot`
运行合成自检及 CSV 输入测试，均为 `frames=6 tracking=6 imu_fused=6`，位姿均为有限值。
可执行文件约 5.5 MB，压缩包约 3.0 MB。日志见 `scripts/schurvins/board-test-gcc11-static.log`。
板上安装目录为 `/mnt/data/schurvins-gcc11-static`。

尚未验证真实 VIO 精度。
未剥离符号的程序和静态核心库位于构建缓存的 `core/` 下。

## 构建要求与功能范围

`build.sh` 默认使用 GCC 11 全静态构建，仍接受 `--gcc11-static` 参数。
主机需要 Git、curl、CMake >= 3.15、make、Boost 1.74 开发头文件（`libboost1.74-dev`）。
也可用 `BOOST_HEADERS=/path/to/boost` 指定头文件目录，或用 `JOBS=2` 降低并行度。
使用 `-mcpu=cortex-a53`、C++14，SchurVINS 核心保留栈帧用于 perf。
依赖 SDK Eigen 3.3.7；其它依赖下载版本见 `sources.lock`，Boost 仅使用头文件。

此版本编译完整双目跟踪前端及 SchurVINS 估计器，启用 IMU 融合。
不构建原 ROS 1/catkin 节点、RViz、回环、全局地图、Ceres 后端、CUDA 和依赖 OpenGV 的单目初始化。
可选 micro-ROS 使用独立通信线程；无需板端安装完整 ROS 2。
FAST 使用通用 FAST-10 实现；OpenCV/Eigen 使用 AArch64 NEON。
运行程序在单核 A53 上使用单线程 OpenCV、同步深度滤波，限制关键帧和特征数量。

## 上板运行

将整个产物目录复制到板子（需约 6 MB 空间）：

```sh
scp -O -r install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static root@192.168.123.99:/mnt/data/
ssh root@192.168.123.99
cd /mnt/data/schurvins-gcc11-static
./run.sh --help
./run.sh --self-test synthetic.yaml
```

自检生成 320×240 合成立体图像和静止 IMU，
执行六帧双目跟踪与 SchurVINS 融合，检查融合标志及位姿有限值。
`synthetic.yaml` **只供自检，绝非 TideSense 标定参数**。

## 实时 VIO + micro-ROS 位姿发布

以下为当前 TideSense 已验证的配置：板端 `192.168.123.99`，电脑端
`192.168.123.2`，UDP `8888`，ROS domain `0`。更换电脑时将 Agent 地址改为实际地址。
需要带 micro-ROS 的程序包；`./run.sh --help` 应显示 `micro-ROS compiled: yes`。

### 1. 电脑端启动 Agent

在电脑终端 A 执行（如果端口 8888 已有 Agent，直接复用，不要重复启动）：

```bash
cd /home/ncer/sophpi
source scripts/calibration/ros2-env.sh
export FASTRTPS_DEFAULT_PROFILES_FILE=/home/ncer/sophpi/scripts/schurvins/micro_ros/calibration-agent.xml
ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

环境脚本加载 ROS 2 Humble、SDK 内的 Agent 和 Cyclone DDS，设置 domain `0`。
当前 DDS 配置用于这台电脑上的本地订阅；在另一台电脑订阅还需配置 DDS 网络发现。

### 2. 板端启动 VIO

使用转换后的 SchurVINS YAML，不能直接传入 Kalibr camchain。
[转换方法](../calibration/README.md#将-kalibr-结果用于-schurvins)见标定说明。
当前已上传的配置为 `tidesense-20260922-221039.yaml`；更新标定时替换此文件名。

先停止已有 VIO、标定采集和 IMU 测试；关闭 PFC 实时预览网页，等待约 3 秒释放 IMU。
启动前确认板端 `date` 正确，ROS 时间戳使用板端系统时间；校准时间偏移不会自动同步两台机器的时钟。
在板端执行：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
vio_output=/mnt/data/vio-$(date +%Y%m%d-%H%M%S)
mkdir -p "$vio_output"
export SCHUR_MICRO_ROS=1
export SCHUR_MICRO_ROS_AGENT=192.168.123.2
export SCHUR_MICRO_ROS_PORT=8888
export SCHUR_MICRO_ROS_DOMAIN=0
export SCHUR_IMU_CALIBRATION=/mnt/data/penguin-flight-console/accelerometer-calibration.json

nohup ./run.sh --vio tidesense-20260922-221039.yaml \
  /mnt/data/sensor_cfg.ini auto "$vio_output/poses.csv" 0 \
  >"$vio_output/vio.log" 2>&1 </dev/null &
echo $! >/tmp/vio-micro-ros.pid
echo "VIO PID: $(cat /tmp/vio-micro-ros.pid); output: $vio_output"
```

`auto` 查找 SC7U22 IIO 设备，`0` 表示持续运行。启动时保持静止，并让双目共同看向
有纹理的场景。六面校准 JSON 在启动时加载；日志应显示 `calibration applied`、
`Camera timestamp offset_ns=...` 和 `micro-ROS: connected ... domain=0`。

当前 `/mnt/data` 已取消 `sync` 挂载，日志和轨迹写入 SD 数据分区，避免持续占满 `/tmp`。
每次启动创建新的输出目录。若使用旧镜像，先检查 `mount` 中 `/mnt/data` 是否仍含 `sync`，
同步写盘会拖慢 VIO。关闭 micro-ROS 时设置 `SCHUR_MICRO_ROS=0` 即可独立运行。

停止本次后台 VIO（在板端执行）：

```sh
kill -INT "$(cat /tmp/vio-micro-ros.pid)"
```

### 3. 电脑端订阅位姿和跟踪状态

电脑另开终端 B，**每个新终端都先加载环境**：

```bash
source /home/ncer/sophpi/scripts/calibration/ros2-env.sh
ros2 topic echo /tidesense/pose \
  geometry_msgs/msg/PoseStamped \
  --qos-reliability best_effort --no-daemon
```

查看跟踪状态（另开已加载同一环境的终端，或先 Ctrl-C 退出位姿订阅）：

```bash
ros2 topic echo /tidesense/tracking \
  std_msgs/msg/Bool \
  --qos-reliability best_effort --no-daemon
```

- 位姿为 **body/IMU 到 odom 的变换**，`header.frame_id=odom`，位置单位米，四元数为 xyzw。
  它不是左相机位姿；此模式不发布图像、TF、速度或协方差。
- `tracking.data=true` 时发布有效跟踪阶段的位姿；失去跟踪时不发布位姿。
  Agent 断连时状态消息也会中断，接收端应检测消息超时。
- 发布使用 best-effort、volatile QoS，订阅需兼容。VIO 重启/重置后局部坐标系可能变化。
- **保留 `--no-daemon`**：当前电脑曾出现普通 `ros2 topic echo` 卡在连接本机
  `127.0.0.1:11511` 的 ROS 2 CLI daemon，连 `ros2 daemon stop` 也等待不返回；
  `--no-daemon` 绕过该路径后已实测正常收到数据，不需要重启板端 VIO 或 Agent。

2026-09-22 当前标定上板复测：20 秒接收 401 条位姿，约 20 Hz。
这验证了发布/订阅链路，不代表完成运动定位精度验证。

## 录制现场输入并离线 replay

为复现初始化后转动/移动发散，可在 `--vio` 命令末尾增加 `--record NEW_DIR`：

```sh
cd /mnt/data/schurvins-gcc11-static-micro-ros
record_dir=/mnt/data/vio-record-$(date +%Y%m%d-%H%M%S)
SCHUR_MICRO_ROS=0 ./run.sh --vio tidesense-20260922-221039.yaml \
  /mnt/data/sensor_cfg.ini auto "$record_dir/live-poses.csv" 0 --record "$record_dir"
# Ctrl-C 正常结束录制后：
SCHUR_MICRO_ROS=0 ./run.sh --replay "$record_dir" "$record_dir/replay-poses.csv"
```

不要提前创建录制目录；父目录需存在。录制默认关闭，micro-ROS 可独立开启。
保存 VIO 实际消费的双目灰度图、连续 IMU、左右原始时间戳、帧序号和标定快照。
回放不会再次应用六面系数或时间偏移。相机消费队列中已被替换的帧不包含在录制中。
20 Hz 约 614 MB/分钟；写盘跟不上或空间不足会明确报错，异常录制需显式
`--allow-incomplete` 才尝试回放。详见 [录制和回放说明](RECORDING.md)。

## 离线输入

```sh
./run.sh actual-calibration.yaml stereo.csv imu.csv poses.csv
```

- 标定 YAML 使用上游格式，参考源码 `svo_ros/param/calib/euroc_stereo.yaml`，必须包含
  `cameras`、`T_B_C`、`imu_params`、`imu_initialization`。
- TideSense 左相机（0x10，2 lane）放第一个，右相机（0x60，1 lane）放第二个。
  50 mm 基线不能替代双目内参、畸变、外参及相机—SC7U22 IMU 的实际标定。
- `stereo.csv` 每行：`timestamp_ns,left_image,right_image`，图像路径相对该 CSV 目录或绝对路径。
  图像必须已配对、同一曝光时刻，尺寸与标定一致。
- `imu.csv` 每行：`timestamp_ns,gx,gy,gz,ax,ay,az`，角速度 rad/s、加速度 m/s²（含重力）。
  坐标系对应标定的 IMU/body 坐标系。输入为原始测量，噪声及初始偏置在标定中设置。
- 时间戳均为严格递增整数纳秒。应先在采集/转换端对齐时钟和延时，`delay_imu_cam` 设为 0；
  IMU 数据必须覆盖首帧之前到末帧之后。CSV 不含普通表头，可用 `#` 注释。
- 输入流式读取，不把整段数据集装入板子内存。
- 输出 `poses.csv`：`timestamp_ns,px,py,pz,qx,qy,qz,qw`，body/IMU 到世界坐标系的位姿，位置单位米。
  仅输出 Tracking 阶段位姿；未产生任何跟踪/融合输出时返回非零。

离线入口保留。CVITEK 实时双目输入现已接入，详见 [CAMERA.md](CAMERA.md)；SC7U22 200Hz 实时融合用法见 [IMU.md](IMU.md)。未验证真实场景定位精度。

## 补丁与许可

`patches/0001-native-build.patch` 修复 FAST 标量分支的 `L` 未定义，
将 ROS 参数头限制在 ROS 构建中，删除未使用的 `opencv2/optflow.hpp` 包含；不改 SchurVINS 的估计公式。
许可证与版权声明保留在下载源码及产物 `licenses/` 中，上游主项目为 GPL-3.0，继承的组件保留各自声明。

离线读取路径的合成测试输入可在主机生成：

```sh
python3 scripts/schurvins/make-smoke-data.py install/schurvins-build/smoke-data
# 把 smoke-data 复制到板子后：
./run.sh synthetic.yaml smoke-data/stereo.csv smoke-data/imu.csv smoke-data/poses.csv
```

## GCC 6 兼容补丁清理

已恢复上游 `std::atomic_uint64_t` 声明和定义，并从补丁文件移除对应改写。
GCC 11 原生支持该别名；当前构建入口不再使用 GCC 6。
原有 GCC 6 构建产物和 `board-test.log` 仅作为历史记录保留。

## A53 NEON optimizations (independent, default OFF)

Each CMake option enables the identically named preprocessor macro with value `1`.
Undefined or `0` keeps the original implementation. These options require AArch64;
no global `__ARM_NEON__` override is used, so the old alignment implementation is
not accidentally selected.

| Option / macro | Scope |
|---|---|
| `SCHUR_OPT_HALFSAMPLE_NEON` | Exact 2×2 box-average downsampling, including stride and tails |
| `SCHUR_OPT_ZMSSD_NEON` | 8×8 template sums and contiguous/strided ZMSSD scoring |
| `SCHUR_OPT_FAST10_NEON` | FAST-10 detection and scoring; original NMS remains in use |

Enable any subset through `build.sh`, for example:

```sh
SCHUR_OPT_HALFSAMPLE_NEON=ON \
SCHUR_OPT_ZMSSD_NEON=ON \
SCHUR_OPT_FAST10_NEON=ON \
./scripts/schurvins/build.sh
```

For direct CMake builds use `-DSCHUR_OPT_HALFSAMPLE_NEON=ON`, etc. The ZMSSD macro
is PUBLIC because its implementation is in a header; consumers must use the same
setting to avoid inconsistent template definitions. Other two macros apply only
to the relevant source files. `0002-a53-opt-in-neon.patch` reproduces the source
changes on the pinned upstream checkout after `0001-native-build.patch`.
Existing local SchurVINS commits descending from the pinned revision are accepted
by the build script and are recorded in `BUILD-INFO`; dependency revisions remain
pinned.

### Reproduce the comparison

After preparing dependencies with `build.sh`:

```sh
./scripts/schurvins/bench/build-variants.sh
# On the board, prepare calib.yaml and stereo.csv in /mnt/data/schur-opt-test.
# CSV format is the same as schurvins_stereo; use absolute image paths.
# Configure SSH keys, or export SSHPASS and install sshpass on the host.
./scripts/schurvins/bench/run-board.sh base half zmssd fast all base
./scripts/schurvins/bench/summarize.py install/schurvins-gcc11-static/opt-results
```

The runner expects the existing EuRoC replay under
`/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay`.
Override `BOARD`, `REMOTE_DIR`, `DATASET`, or `RESULTS_DIR` if needed. It uploads only
to the dedicated benchmark directory and leaves the installed production program
untouched. Keep other CPU workloads idle during measurement. Raw results are
copied back before the runner removes its own completed logs from the board.

`SCHUR_BUILD_BENCHMARKS=ON` builds `schurvins_kernel_bench`. It checks scalar/NEON
agreement, then reports medians of seven batches per kernel for each supplied
image. The actual production functions are used. The replay program optionally
writes per-frame `addImageBundle` wall time to the path in `SCHUR_TIMING_CSV`;
image decoding and CSV input are outside this interval. The summary excludes the
first five calls and reports mean, median, P95, tracking counts and pose-file hash.
These timings measure VIO processing, not camera-to-pose latency or VIO accuracy.

### CPU 热点复测与输出位置

当前 benchmark 将日志、位姿和计时写入板端 `/tmp`（tmpfs），完成后拉回宿主机。
不要把热路径日志重定向到以 `sync` 挂载的 `/mnt/data`；之前这样测得的约 2% 整体收益被日志写入等待掩盖。
修正后的无 perf 测量为 66.578 → 58.089 ms，耗时下降 12.75%，详见 [实测结果](bench/RESULTS.md)。

额外设置 `SCHUR_PROFILE_CSV=/tmp/intervals.csv` 可导出每帧 VIO 单调时钟区间和进程 CPU 时间。
重现 perf 采样、匹配 ELF、过滤 VIO 区间和 period 加权统计的步骤见 [采样说明](profile/README.md)，
当前热点和调用来源见 [热点复测](profile/HOTSPOTS.md)。

### align2D 浮点 NEON（独立、默认 OFF）

实板结果见 [align2D 实测](bench/ALIGN2D.md)。

`SCHUR_OPT_ALIGN2D_NEON=ON`（CMake 同名选项，预处理宏值为 `1`）启用
AArch64 浮点插值/残差 SIMD。未定义或为 `0` 保留原路径；`no_simd=true`
强制标量。支持 offset 开/关；gain 估计开启时保留标量路径，以避免该模式下的
浮点舍入差异。当前 VIO 默认 offset 开、gain 关，会使用优化。
保持原 Hessian、逆矩阵、迭代上限、收敛阈值、逐像素累加顺序和 `each_step`。
没有启用旧定点 `align2D_NEON`，也没有修改全局 NEON 宏或开启 fast-math。

```sh
SCHUR_OPT_HALFSAMPLE_NEON=ON SCHUR_OPT_ZMSSD_NEON=ON \
SCHUR_OPT_FAST10_NEON=ON SCHUR_OPT_ALIGN2D_NEON=ON \
./scripts/schurvins/build.sh
```

源码补丁为 `0003-a53-align2d-neon.patch`。精确一致性以当前 GCC 11.3.1 / A53
构建的测试为依据；换编译器或浮点选项后应重新验证。

单独比较 align2D（两组均开启之前三项优化）：

```sh
./scripts/schurvins/bench/build-align2d.sh
./scripts/schurvins/bench/run-align2d-check.sh
BINARIES_DIR="$PWD/install/schurvins-gcc11-static/align2d-binaries" \
RESULTS_DIR="$PWD/install/schurvins-gcc11-static/align2d-results" \
./scripts/schurvins/bench/run-board.sh base align base align
./scripts/schurvins/bench/summarize.py install/schurvins-gcc11-static/align2d-results
```

`schurvins_align2d_bench` 为额外板端测试程序：无参数执行 32,000 组状态、坐标、
每步迭代一致性检查；可传入真实灰度图路径，追加 patch 检查和局部计时。
它使用独立编译的宏关闭实现作参考，并同时核对优化与 `no_simd` 两条路径。
测量输出写 tmpfs，计时区间不含日志写入和图片解码。

### align2D Hessian 与 warpAffine（各自默认 OFF）

| 宏 / CMake 选项 | 范围 |
|---|---|
| `SCHUR_OPT_ALIGN2D_HESSIAN_NEON` | NEON 整数梯度/Hessian 统计；gain 关闭且 no_simd=false 时启用，offset 可开/关；保留原 4×4 求逆和迭代 |
| `SCHUR_OPT_WARP_AFFINE_NEON` | 10×10 patch 浮点仿射取样，四像素一批，安全处理两像素尾部；其他尺寸、重叠缓冲区与异常条件回退 |

宏未定义或为 `0` 时保留原路径，CMake `ON` 定义为 `1`。Hessian 宏独立于
`SCHUR_OPT_ALIGN2D_NEON`，测试包括只开 Hessian、不开旧插值 SIMD 的组合。
`no_simd=true` 同时禁用 align2D 的两项优化。源码修改分别位于补丁 `0004`、`0005`。
warp 保持原 floor、FMA、权重和 uint8 截断顺序；边界失败回到原路径以保留部分写入行为。

生成六项全开静态包：

```sh
SCHUR_OPT_HALFSAMPLE_NEON=ON SCHUR_OPT_ZMSSD_NEON=ON \
SCHUR_OPT_FAST10_NEON=ON SCHUR_OPT_ALIGN2D_NEON=ON \
SCHUR_OPT_ALIGN2D_HESSIAN_NEON=ON SCHUR_OPT_WARP_AFFINE_NEON=ON \
./scripts/schurvins/build.sh
```

复现单项和组合测试（提前准备现有板端数据、SSH key 或 `SSHPASS`）：

```sh
./scripts/schurvins/bench/build-next.sh
./scripts/schurvins/bench/run-next-check.sh both
BINARIES_DIR="$PWD/install/schurvins-gcc11-static/next-binaries" \
RESULTS_DIR="$PWD/install/schurvins-gcc11-static/next-results" \
./scripts/schurvins/bench/run-board.sh base hessian warp both both base
./scripts/schurvins/bench/summarize.py install/schurvins-gcc11-static/next-results
```

这里 base 是之前四项全开的基线；hessian/warp/both 分别为新项 1/2/两者。
不要与旧 `opt-binaries/base`（原优化全关）混淆。构建脚本也支持参数只构建指定组合。
实测和第 3 项的最终汇编评估见 [HESSIAN-WARP.md](bench/HESSIAN-WARP.md)。

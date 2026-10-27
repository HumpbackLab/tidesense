# tide_cam

面向 TideSense 的进程内相机采集公共库。公共头文件是纯 C API，不暴露 SoC、厂商 SDK、
I2C、MIPI lane、VI 通道或 OpenCV 类型；后端在构建时选择。目前实现 SG2002，
以后可为 RV1103 添加后端并复用公共接口和缓冲管理。不同 CPU 架构需要分别编译。

当前 SG2002 后端支持双 OV9281，camera 0 为左相机，camera 1 为右相机；
右相机保持原始安装方向。以下尺寸均输出 mono8，支持硬件同步和自由运行：

| 尺寸 | 传感器取景方式 |
| --- | --- |
| 640×400（默认） | 全视场子采样 |
| 640×480 | 中央窗口裁剪，不是对 640×400 拉伸 |
| 1280×800 | 全分辨率 |

模式能力通过 `tide_cam_query_caps()` 查询，不支持的尺寸/帧率明确返回
`TIDE_CAM_UNSUPPORTED`。`mode` 仅指定尺寸和像素格式，不包含帧率。

- **硬件同步**：帧率由触发源决定。当前 SG2002 板级触发为 IMU 200 Hz / 10，
  名义 20 Hz，通过只读 `caps.hardware_sync_fps` 查询；调用方没有同步帧率设置。
  config 的 `free_run_fps` 必须保持 `{0,0}`，传入任何值都会在启动硬件前明确报错。
- **自由运行**：使用独立的 `config.free_run_fps = {分子, 分母}` 设置。
  当前三个尺寸均接受名义 **1.67..60 fps**，默认 20 fps；通过
  `caps.free_run_fps_min/max/default` 查询。支持整数及有理数，如 `{30,1}`、`{30000,1001}`。
  库通过 ISP/驱动调整传感器帧长，没有在采集后抽帧。

低于 20 fps 的请求先以 20 fps 完成 VI 启动，收到启动图像后再切换到请求频率并丢弃过渡帧；
调用方收到的是完成切换后的图像。此处理避免 SDK 在低频下等待过多启动帧。

频率均为名义值，传感器时钟、行周期量化、双目配对和处理负载会影响实际交付频率。
切换分辨率或频率需要 close 后重新 open。60 fps 的传感器配置不等于 60 Hz 硬件触发支持。

## 采集与时间戳

```text
SG2002 VI → 缓存失效 → 一次复制到库的缓冲池 → 帧引用 → 调用方
```

库在本进程的后台线程中采集，没有常驻相机子进程、socket 传图或图像编码。
应用通过结构体传参；库不会自动读取配置文件，也不调用 `schurvins_camera`、`fsin.sh`、ROS 或 VIO。
板级接线及 SDK 配置完全封装在 `src/sg2002/` 中，不依赖 `scripts/schurvins/` 的文件。
SDK 自身仍有初始化日志、可选 tuning bin 读取及历史 `devmem` shell 调用；这些不属于
库的配置入口，也不承诺整个厂商 SDK 初始化过程完全没有临时子进程。

每颗相机的 `timestamp_ns` 均为 **VI 到达时间 `u64PTS × 1000`**，单位纳秒，
时钟域为当前 TideSense 内核的 `CLOCK_MONOTONIC`。帧组时间取 camera 0 的 VI 时间，
保留两路各自时间戳，不改成相同值，不应用旧标定时间偏移。它不是曝光中心或 FSIN 边沿时间。
`sensor_sequence` 保留各路 VI `u32TimeRef`，组 `sequence` 从 1 递增。

默认硬件同步由 SC7U22 INT1 每 10 次中断触发 FSIN，要求加速度计和陀螺仪均为 200 Hz；
库直接操作内核 sysfs，不启用 IMU IIO buffer，也不修改 IMU 采样率。
可选择 `TIDE_CAM_SYNC_NONE` 自由运行，此时仅按到达时间近邻配对，不保证曝光同步。
`sync` 表示配置的同步方式，不是独立测量的同步精度。

## 使用

```c
#include <tide_cam.h>

tide_cam_config config;
tide_cam_config_init(&config); /* 默认硬件同步，频率由触发源决定，不填写 fps */
/* 可选：config.mode.width = 1280; config.mode.height = 800; */

tide_cam *camera = NULL;
char error[256];
int status = tide_cam_open(&config, &camera, error, sizeof(error));
if (status != TIDE_CAM_OK) {
    /* 处理 status 和 error。 */
    return status;
}

const tide_cam_frame_set *frames;
status = tide_cam_acquire(camera, &frames, 1000);
if (status == TIDE_CAM_OK) {
    /* 读取 frames->frames[i]，data 在 release 前持续有效。 */
    /* 在此完成处理，或持有整组引用到异步处理结束。 */
    tide_cam_release(camera, frames);
}
tide_cam_close(camera);
```

自由运行的初始化与频率设置：

```c
tide_cam_config config;
tide_cam_config_init_free_run(&config); /* 自由运行，默认 20 fps */
config.free_run_fps = (tide_cam_rate){30, 1};
/* 后续 open/acquire/release/close 与同步模式一致。 */
```

公共接口只维护当前这一份定义，通过初始化函数设置默认采集参数。
直接把 `sync` 改为 NONE 时，也必须填写有效的 `free_run_fps`。

`acquire` 的超时参数单位为毫秒，`-1` 无限等待、`0` 立即查询。
每次成功 acquire 必须且只能 release 一次，release 后不能再访问旧指针。
可以直接用指针/stride 构造不拥有内存的 OpenCV Mat，但必须在所有使用者结束后才 release。

- `TIDE_CAM_LATEST`：未被 acquire 的旧帧组会被替换，`stats.replaced` 统计替换数量；
  已租借的图像不会被覆盖。适合实时 VIO、预览和深度。
- `TIDE_CAM_FIFO`：按顺序交付；缓冲耗尽、稳态 VI 帧序/时间异常或配对丢失时停止采集并报错，
  不静默丢帧。适合录制。启动丢弃 4 组暖机帧，连续性检查从首个有效帧组开始。
- `buffer_count` 为 3..64，包含待交付、调用方持有以及采集线程正在填充的帧组。
  默认 4 组双目像素在 640×400 / 640×480 / 1280×800 下分别占约
  2.05 / 2.46 / 8.19 MB（不含 SDK VI/ISP 缓冲）；调用方应及时释放帧。所有缓冲都被持有时，
  最新帧模式也会报告 `TIDE_CAM_OVERFLOW`。
- `tide_cam_stop()` 唤醒阻塞的 acquire，等待采集线程退出并释放 FSIN/ISP/VI，
  已租借图像仍有效。队列中的有效帧可继续取完，随后 acquire 返回错误或 STOPPED。
- 存在未 release 的帧时，`tide_cam_close()` 返回 BUSY，并保持句柄有效。
  全部释放后再 close；close(NULL) 可用。stop/close 需由调用方串行执行，close 时不能有其他 API 调用。
- acquire/release/stats/error 可并发调用，但它们共享同一条消费流，不提供多订阅者广播。
  库不安装应用信号处理器；不要在信号处理函数中调用 stop/close。
- 采集错误通过 acquire、stats、get_error 获取；stop/close 返回的是硬件清理结果。
  出错后后台线程也会主动清理硬件。

默认 ISP 与现有 TideSense OV9281 profile 一致：自动曝光/增益、曝光上限 500 us、
YNR 192、关闭锐化/TNR。`exposure_max_us` 可在 open 前指定，必须为正且不大于名义帧周期；
例如自由运行 60 fps 时不能设置 20000 us。实际曝光仍受驱动上限约束并按行周期量化。
库不缩放、不旋转、不去畸变。

设备运行设置使用独立的 `key=value` 文件，目前只接受 `exposure_max_us=1..50000`。
调用方可在 `tide_cam_config_init()` 后显式调用 `tide_cam_config_load_file(path, &config, error, size)`；
文件缺失返回 `TIDE_CAM_NOT_FOUND`，保持默认值，格式错误则返回错误且不修改 config。
录制器和 SchurVINS 默认读取 `/mnt/data/tide-configurator/camera.conf`，可通过
`TIDE_CAM_CONFIG_PATH` 指定其他路径。`tide_cam_settings get|set PATH [值]` 使用同一解析函数，
其中 `set` 原子替换设备文件；修改后重新启动采集生效。

## 构建 SG2002

在工程根目录执行：

```sh
tide_cam/build.sh
```

使用 SDK GCC 6 构建所需 SDK archives 及库，不构建 SchurVINS/OpenCV/ROS。
构建脚本会应用 `patches/sg2002-isp-mono.patch` 并重建 `libisp.a`：RGB CAC
遵从用户配置，TNR 关闭时仍给内核提供合法的 RGB map 块大小。旧版 ISP 在 OV9281
关闭去马赛克和 TNR 后会反复打印两条警告；重复构建不会重复应用补丁。
默认产物：

```text
install/tide_cam/
  include/tide_cam.h
  lib/libtide_cam.a
  lib/cmake/tide_cam/
  bin/tide_cam_capture
  bin/tide_cam_settings
```

也可在已生成 SDK archives 后，用 GCC 11 构建全静态应用：

```sh
cmake -S tide_cam -B install/tide_cam-gcc11-static \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tide_cam/cmake/sg2002-gcc11.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static \
  -DTIDE_CAM_BUILD_BOARD_TESTS=ON
cmake --build install/tide_cam-gcc11-static -j4
```

GCC 6 版本的 SDK 库仍动态依赖设备 libc；GCC 11 的上述示例为全静态。
库本身为静态 archive，厂商 archives 在最终应用链接时通过 CMake target 自动带入。
安装的 CMake target 引用当前 SDK archives 的绝对路径，适合同一 SDK 工作区使用；
它不是脱离 SDK 即可转移到另一台电脑的独立二进制开发包。

同一 CMake 工程可使用：

```cmake
add_subdirectory(/path/to/sophpi/tide_cam tide_cam-build)
target_link_libraries(my_app PRIVATE tide_cam::tide_cam)
```

或将 `install/tide_cam` 加入 `CMAKE_PREFIX_PATH` 后 `find_package(tide_cam CONFIG REQUIRED)`。
通过导出 target 链接会自动带入 SDK 所需的链接顺序和通道深度 wrapper；不要仅链接一个 `.a`
而漏掉这些依赖。`TIDE_CAM_BUILD_DIR`、`TIDE_CAM_INSTALL_DIR`、`TIDE_CAM_TOOLCHAIN`
可调整 build.sh 的构建目录、安装目录和工具链。

## 设备运行

先停止占用相机或 IMU/FSIN 的其他采集应用。当前后端独占 SDK SYS/VI/ISP，
不能与另一个初始化相同 MPI 全局资源的程序或库共用硬件。
沿用 `/run/tidesense-camera.lock` 与 `/run/tidesense-fsin.lock`，与现有采集入口互斥；
不遵守这些锁的旧 SDK sample 不受保护。设备需具有匹配的相机驱动、SC7U22 INT1/FSIN 内核
及访问 `/dev`、`/run`、sysfs 的权限，通常以 root 运行。不需要部署 INI 或辅助脚本。

将 `tide_cam_capture` 复制到板端，例如：

```sh
./tide_cam_capture --frames 100
./tide_cam_capture --frames 100 --fifo
./tide_cam_capture --width 640 --height 480 --frames 100
./tide_cam_capture --width 1280 --height 800 --frames 100 --fifo
./tide_cam_capture --width 1280 --height 800 --frames 100 --free-run
./tide_cam_capture --free-run --fps 30 --frames 100
./tide_cam_capture --free-run --fps 30000/1001 --frames 100
./tide_cam_capture --frames 0  # Ctrl-C 后在主线程正常关闭
```

`--fps` 只在 `--free-run` 下有效，参数顺序不限。在默认同步模式下传入 `--fps` 会直接报错，
不会打开设备或把指定频率静默忽略。低频自由运行的取图超时、配对窗口和 FIFO 间隔检查
按实际请求帧周期计算。

切换尺寸会改变采样几何或视场，VIO/标定调用方应使用匹配实际尺寸的标定参数。
库返回实际 width/height/stride/data_size，调用方不要按默认尺寸固定分配或解释图像。

主机 SDK 日志中既有的 `devmem: not found`、AF 未初始化及 netlink family 消息可能出现；
判断采集结果应检查应用退出码、有效图像、帧序、频率和清理结果。
SIGKILL/断电无法执行库的正常清理。

## 验证

主机测试使用独立 mock 后端，不依赖 SDK 或真实硬件：

```sh
cmake -S tide_cam -B install/tide_cam-host-tests \
  -DTIDE_CAM_BACKEND=TEST -DTIDE_CAM_BUILD_TESTS=ON \
  -DTIDE_CAM_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build install/tide_cam-host-tests -j4
ctest --test-dir install/tide_cam-host-tests --output-on-failure
```

覆盖同步模式拒绝频率设置、自由运行范围/有理数/非法值、
三种尺寸的像素缓冲边界/长度、逐像素租借有效性、左右独立时间戳、最新帧替换、FIFO 顺序和溢出、
重复 release、未释放时拒绝 close、stop 唤醒及后台故障清理。
`TEST` 后端仅用于上述测试，不是可部署的采集实现。

`TIDE_CAM_BUILD_BOARD_TESTS=ON` 另构建 `tide_cam_board_test`，需复制到 SG2002 执行，
遍历所有已声明尺寸，测试真实双目、LATEST/FIFO、实际尺寸/步长/长度、持有图像不被覆盖、
忙设备拒绝、停止后帧有效、同进程关闭后换尺寸重开和信号处理器保持。
验证记录见 [VALIDATION.md](VALIDATION.md)。

`tidesense_record` 与 SchurVINS 实时入口已接入此库，分别使用 FIFO 和最新待处理帧策略。
两者通过应用适配层复制到独立 OpenCV Mat 后释放库帧，支持算法关键帧和后台写盘持有图像，
避免过早复用采集缓冲。depth 等其他现有应用仍使用原入口。
标定复用 `tidesense_record`，流程见 [标定说明](../scripts/calibration/README.md)。

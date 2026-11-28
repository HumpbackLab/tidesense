# tide_cam

面向 TideSense 的进程内相机采集公共库。
当前 SG2002 后端支持双 OV9281，camera 0 为左相机，camera 1 为右相机；
右相机保持原始安装方向。以下尺寸均输出 mono8，支持硬件同步和自由运行：

| 尺寸 | 传感器取景方式 |
| --- | --- |
| 640×400（默认） | 全视场子采样 |
| 640×480 | 中央窗口裁剪，不是对 640×400 拉伸 |
| 1280×800 | 全分辨率 |

模式能力通过 `tide_cam_query_caps()` 查询，不支持的尺寸/帧率明确返回
`TIDE_CAM_UNSUPPORTED`。`mode` 仅指定尺寸和像素格式，不包含帧率。

- **IMU 触发**（`TIDE_CAM_SYNC_HARDWARE`）：帧率由触发源决定。当前 SG2002 板级触发为 IMU 200 Hz / 10，
  名义 20 Hz，通过只读 `caps.hardware_sync_fps` 查询；调用方没有同步帧率设置。
  config 的 `fps` 必须保持 `{0,0}`，传入其他值会在启动硬件前明确报错。
- **自由运行**：使用独立的 `config.fps = {分子, 分母}` 设置。
  当前三个尺寸均接受名义 **1.67..60 fps**，默认 20 fps；通过
  `caps.free_run_fps_min/max/default` 查询。支持整数及有理数，如 `{30,1}`、`{30000,1001}`。
  库通过 ISP/驱动调整传感器帧长，没有在采集后抽帧。
- **传感器主同步**：在配置中选择 `TIDE_CAM_SYNC_SENSOR_MASTER`，设置主摄 ID 和有理数帧率，
  再调用 `tide_cam_open()`。分辨率由 `config.mode` 配置。TideSense 的 ID 0 为左目，1 为右目。
  两目共用 FSIN，由选定的主摄输出，SG2002 记录边沿并据此配对双目。
  普通速率沿用 60 fps 以内的 ISP 路径。当前高速组合为 640×480 @180 和 640×400 @210，
  使用 VI 前端 DMA 输出 mono8，需要启用 FSIN 输入事件，且加载 `cv181x_vi.ko` 时传入
  `tidesense_fe_only=1`。功能由 SDK 的 `osdrv/interdrv/vi/chip/cv181x` 提供。
  普通速率使用模块的默认 ISP 后端模式。

## 采集与时间戳

默认要求完整双目。诊断预览可设置 `config.allow_partial=1`，此时每组只包含本次收到的图像，
通过 `camera_count` 和每帧的 `camera_id` 判断左右相机；缺失相机不会阻塞另一侧。
该模式不能与直接 VI JPEG 编码同时启用，录制和 VIO 应保留默认的完整双目要求。

```text
SG2002 VI → 缓存失效 → 一次复制到库的缓冲池 → 帧引用 → 调用方
```

每颗相机的 `timestamp_ns` 为 `CLOCK_MONOTONIC` 纳秒。普通路径记录 VI 到达时间，
高速前端 DMA 路径记录 VI 帧起始时间（`TIDE_CAM_TIMESTAMP_VI_SOF`）。帧组时间取左目的 VI 时间，
保留两路各自时间戳，`sensor_sequence` 保留各路 VI `u32TimeRef`，组 `sequence` 从 1 递增。

默认硬件同步由 SC7U22 INT1 每 10 次中断触发 FSIN，要求加速度计和陀螺仪均为 200 Hz；
库直接操作内核 sysfs，
可选择 `TIDE_CAM_SYNC_NONE` 自由运行，此时仅按到达时间近邻配对，不保证曝光同步。
`sync` 表示配置的同步方式，不是独立测量的同步精度。

传感器主同步交付完整双目，`fsin_sequence` 和 `fsin_timestamp_ns` 标识配对使用的边沿，
`exposure_ns` 是两目共同的曝光时长。主摄选择与帧数组顺序独立。
帧有效期与现有 `acquire/release` 相同。并排布局由调用方处理；现有 JPEG 接口返回同一帧组的左右独立 JPEG。

## 使用

```c
#include <tide_cam.h>

tide_cam_config config;
tide_cam_config_init(&config, sizeof(config)); /* 默认 IMU 触发，帧率由触发源决定 */
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

## 可选的双目硬件 JPEG

`config.jpeg_quality` 默认 `0`（关闭）；设置为 `1..99` 启用左右独立、原尺寸的 JPEG 编码。
采集帧率仍由原来的硬件触发或自由运行配置决定，没有 JPEG 限速器。

```c
config.jpeg_quality = 80; /* 在 tide_cam_open 前设置 */

/* 在应用自己的 JPEG 工作线程中调用，与 mono8 消费线程并行。 */
const tide_cam_jpeg_pair *pair = NULL;
int status = tide_cam_get_jpeg(camera, &pair);
if (status == TIDE_CAM_OK) {
    /* pair->frames[0/1].data 和 size 是左右各一张完整 JPEG 的只读视图。
     * 在这里发送、序列化或使用；库不提供 ROS、DDS、HTTP 或文件输出。
     * 异步发送必须等外部发送者不再引用这些字节，才能 release。 */
    tide_cam_release_jpeg(camera, pair);
}
```

编码由 `get_jpeg()` 的调用线程执行；库不额外创建编码线程。
SG2002 后端直接将仍被持有的 VI YUV420 物理帧描述符提交 VENC，不拼图、缩放，
不从 mono8/OpenCV 图像复制回来。返回地址直接引用 VENC 的连续码流，库不再复制 JPEG。
当前驱动每张 JPEG 返回一个 packet；左右使用两个独立编码通道及各自的 ES 缓冲。
若系统配置了共享 ES 缓冲（`bSingleEsBuf=1`），开启 JPEG 会明确拒绝，不修改全局编码配置。

一组结果包含 `sequence` 和 `frames[2]`；每张图保留 `camera_id`、尺寸及各自原始 VI 时间戳。
组编号与 mono8 的同源帧组一致。JPEG 时间仍是 monotonic VI arrival，不做标定补偿或 ROS 时间换算。

只允许一个逻辑 JPEG 消费者、一次借出一组。重复取用返回 `TIDE_CAM_BUSY`；
未归还前阻塞下一次采集，不主动替换或丢 JPEG。调用者负责让“编码、发送、归还”整条支路足够快；
慢消费者会拖慢 mono8/VIO，VI 的有限缓冲也可能积压或丢失输入，不能依靠此接口保存无限历史。
编码失败作为相机会话错误上报，不静默降级。

停止时由应用的控制线程调用 `tide_cam_stop()`，阻塞等待新 JPEG 的线程会被唤醒，
返回 `TIDE_CAM_STOPPED`（若已有采集错误则返回该错误）。已经借出的 JPEG 在 stop 后仍有效。
stop 会取消尚未开始编码的输入，停止阶段不保证交付最后一组 JPEG。
归还并结束 JPEG 线程后再 close。存在借用或编码调用时，close 返回 BUSY。
JPEG 开启时，stop 只停止采集，VENC/SYS 资源保留到 close，防止释放应用正在读取的码流。
各 stop/close 调用仍必须由调用方串行化。

自由运行的初始化与频率设置：

```c
tide_cam_config config;
tide_cam_config_init_free_run(&config, sizeof(config)); /* 自由运行，默认 20 fps */
config.fps = (tide_cam_rate){30, 1};
/* 后续 open/acquire/release/close 与同步模式一致。 */
```

## 构建 SG2002

在工程根目录执行：

```sh
tide_cam/build.sh
```
默认产物：

```text
install/tide_cam/
  include/tide_cam.h
  lib/libtide_cam.a
  lib/cmake/tide_cam/
  bin/tide_cam_capture
```

也可在已生成 SDK archives 后，用 GCC 11 构建全静态应用：

```sh
cmake -S tide_cam -B install/tide_cam-gcc11-static \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tide_cam/cmake/sg2002-gcc11.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS=-static \
  -DTIDE_CAM_BUILD_BOARD_TESTS=ON
cmake --build install/tide_cam-gcc11-static -j4
```

同一 CMake 工程可使用：

```cmake
add_subdirectory(/path/to/sophpi/tide_cam tide_cam-build)
target_link_libraries(my_app PRIVATE tide_cam::tide_cam)
```

或将 `install/tide_cam` 加入 `CMAKE_PREFIX_PATH` 后 `find_package(tide_cam CONFIG REQUIRED)`。
`TIDE_CAM_BUILD_DIR`、`TIDE_CAM_INSTALL_DIR`、`TIDE_CAM_TOOLCHAIN`
可调整 build.sh 的构建目录、安装目录和工具链。

## 设备运行
将 `tide_cam_capture` 复制到板端，例如：

```sh
./tide_cam_capture --frames 100
./tide_cam_capture --frames 100 --fifo
./tide_cam_capture --width 640 --height 480 --frames 100
./tide_cam_capture --width 1280 --height 800 --frames 100 --fifo
./tide_cam_capture --width 1280 --height 800 --frames 100 --free-run
./tide_cam_capture --free-run --fps 30 --frames 100
./tide_cam_capture --free-run --fps 30000/1001 --frames 100
./tide_cam_capture --sensor-master right --width 1280 --height 800 --fps 60 --fifo
./tide_cam_capture --sensor-master right --width 640 --height 400 --fps 210 --fifo --buffers 16
./tide_cam_capture --frames 0  # Ctrl-C 后在主线程正常关闭
```

`--sensor-master` 需要 `--fps`，`--free-run` 可选 `--fps`。默认 IMU 触发模式由触发源决定帧率。

从 C API 选择传感器主同步时，复用上面的 `config` 和帧租借接口。

```c
config.sync = TIDE_CAM_SYNC_SENSOR_MASTER;
config.master_camera_id = 1;
config.fps = (tide_cam_rate){60, 1};
int status = tide_cam_open(&config, &camera, error, sizeof(error));
```

`tide_cam_config_init()` 记录调用方的配置结构体大小。后续字段追加在尾部，
新库读取已发布的结构体版本并为缺少的字段使用默认值。
旧库收到无法识别的较大结构体时返回 `TIDE_CAM_UNSUPPORTED`。

传感器主同步的 `tide_cam_stop()` 停止采集，SDK 系统资源保留到
`tide_cam_close()`。如果调用方使用 SDK 编码器，先释放自己的编码器和缓冲。

验证记录与测试命令见 [VALIDATION.md](VALIDATION.md)。

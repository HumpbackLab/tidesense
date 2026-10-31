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

- **硬件同步**：帧率由触发源决定。当前 SG2002 板级触发为 IMU 200 Hz / 10，
  名义 20 Hz，通过只读 `caps.hardware_sync_fps` 查询；调用方没有同步帧率设置。
  config 的 `free_run_fps` 必须保持 `{0,0}`，传入任何值都会在启动硬件前明确报错。
- **自由运行**：使用独立的 `config.free_run_fps = {分子, 分母}` 设置。
  当前三个尺寸均接受名义 **1.67..60 fps**，默认 20 fps；通过
  `caps.free_run_fps_min/max/default` 查询。支持整数及有理数，如 `{30,1}`、`{30000,1001}`。
  库通过 ISP/驱动调整传感器帧长，没有在采集后抽帧。

## 采集与时间戳

```text
SG2002 VI → 缓存失效 → 一次复制到库的缓冲池 → 帧引用 → 调用方
```

每颗相机的 `timestamp_ns` 均为 **VI 到达时间 `u64PTS × 1000`**，单位纳秒，
时钟域为当前 TideSense 内核的 `CLOCK_MONOTONIC`。帧组时间取 camera 0 的 VI 时间，
保留两路各自时间戳，`sensor_sequence` 保留各路 VI `u32TimeRef`，组 `sequence` 从 1 递增。

默认硬件同步由 SC7U22 INT1 每 10 次中断触发 FSIN，要求加速度计和陀螺仪均为 200 Hz；
库直接操作内核 sysfs，
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
./tide_cam_capture --frames 0  # Ctrl-C 后在主线程正常关闭
```

`--fps` 只在 `--free-run` 下有效，参数顺序不限。在默认同步模式下传入 `--fps` 会直接报错。

验证记录与测试命令见 [VALIDATION.md](VALIDATION.md)。

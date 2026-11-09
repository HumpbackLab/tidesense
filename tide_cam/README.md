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

默认要求完整双目。诊断预览可设置 `config.allow_partial=1`，此时每组只包含本次收到的图像，
通过 `camera_count` 和每帧的 `camera_id` 判断左右相机；缺失相机不会阻塞另一侧。
该模式不能与直接 VI JPEG 编码同时启用，录制和 VIO 应保留默认的完整双目要求。

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

## FPN 校正（黑平衡 + 暗场列 FPN + 平场列增益）

OV9281 既有随增益变化的残余黑电平，也有暗场列 FPN 和正常光照下可见的固定细密竖纹。
SG2002 后端启动时按相机加载 `camN.fpn`，在 VI 的 Y 平面内原地执行：

1. 扣除全局黑平衡；
2. 扣除暗场逐列偏移；
3. 乘以平场逐列增益。

暗场偏移按当前帧高于黑电平的平均信号做 `K/(K+信号)` 衰减（`K=48` Y LSB）：
暗场全量扣除，亮场自动减弱，避免暗场标定幅度在非暗室下过扣。平场增益只修正
相对局部基线的细密列响应，不做全列均值归一化，因此保留镜头自然的大尺度明暗分布。
三项都按 AE 上报的模拟增益在相邻标定点之间线性插值，无需关闭自动曝光。
mono8 输出和直接 VI JPEG 都使用同一份已校正像素。

默认目录为 `/mnt/data/ov9281-fpn`，文件名为 `cam0.fpn`（左）和 `cam1.fpn`（右）。
目录中没有 LUT 时静默跳过；文件损坏或尺寸与当前采集模式不一致时打印一次警告并跳过。

是否应用 LUT 由相机设置文件中的可选键控制（`TIDE_CAM_CONFIG_PATH`，默认
`/mnt/data/tide-configurator/camera.conf`，与 `exposure_max_us` 同一文件）：

- `fpn=1`（默认）：启动时加载并应用黑平衡、暗场列 FPN 和平场列增益。
- `fpn=0`：不加载、不应用，即使 LUT 文件存在。

配置文件缺少 `fpn` 时按 `1` 处理。没有单独的命令行开关。
标定工具 `tide_cam_fpn_calib` 内部固定关闭校正，不会把已有 LUT 叠加进新数据。

黑平衡量是 ISP BLC 之后、从 8-bit Y 暗帧测得的残余黑电平；板端 ISP BLC 粗表仍保留，
FPN LUT 负责补偿其残余误差和左右相机差异。

暗场和平场是两次独立采样，但最终只在打包时写入一次 `camN.fpn`。两次采样的曝光可以
不同，但每个增益桶必须使用相同模拟增益。平场增益是乘性列响应，不需要在每个场景
亮度下重标；只需保证标定时均匀光场不过曝。黑电平、暗场偏移和平场增益都会按实际
模拟增益插值，因此应覆盖设备实际会用到的最低、最高增益。

LUT 只针对生成时的采集尺寸（例如 640×400）；切换模式需要在该模式下重新标定。
LUT 也与具体相机、曝光上限和当前增益曲线绑定；换板、换相机、修改
`exposure_max_us` 或 ISP 标定变化后必须重新采集，不能沿用其他设备或其他时段的旧数据。
当前 TFPN 仍为版本 2，但记录布局已改为每个增益桶包含
`black_q8`、`width` 个暗场偏移和 `width` 个平场增益；旧版仅暗场的 v2 LUT
以及 v1 LUT 都不再兼容，需要重新标定。

标定流程（暗场盖住镜头；平场使用均匀、离焦光源，手动固定曝光/增益）：

```sh
# 1. 板端按增益各采一组暗帧；标定工具内部不应用已有 LUT。
mkdir -p /mnt/data/fpn-cal/dark /mnt/data/fpn-cal/flat
cd /mnt/data/fpn-cal
for gain in 1024 2048 4096 7936 15872; do
  /mnt/data/ov9281-update/fpn_calib \
    --mode dark --frames 40 --exposure-us 500 --gain "$gain" \
    --out "dark/g$gain"
done

# 2. 换成均匀离焦光源，保持每个增益的曝光不过曝，再采平场。
for gain in 1024 2048 4096 7936 15872; do
  /mnt/data/ov9281-update/fpn_calib \
    --mode flat --frames 40 --exposure-us 500 --gain "$gain" \
    --out "flat/g$gain"
done
# 上面的 500 us 只是示例；不同增益下应调整到均匀光场约 80..160 LSB 且不过曝。

# 3. 取回主机，先分析暗场，再用同增益暗场分析平场。
for g in 1024 2048 4096 7936 15872; do
  scp -r "root@192.168.123.99:/mnt/data/fpn-cal/dark/g$g" \
    "install/fpn-cal/dark-g$g"
  scp -r "root@192.168.123.99:/mnt/data/fpn-cal/flat/g$g" \
    "install/fpn-cal/flat-g$g"
  python3 scripts/ov9281/fpn_analyze.py "install/fpn-cal/dark-g$g"
  python3 scripts/ov9281/fpn_analyze.py "install/fpn-cal/flat-g$g" \
    --dark "install/fpn-cal/dark-g$g"
done

# 4. 按增益配对打包为 TFPN，并部署到默认目录。
python3 scripts/ov9281/fpn_pack.py \
  --dark install/fpn-cal/dark-g* --flat install/fpn-cal/flat-g* \
  --out install/ov9281-fpn
ssh root@192.168.123.99 mkdir -p /mnt/data/ov9281-fpn
scp install/ov9281-fpn/cam0.fpn install/ov9281-fpn/cam1.fpn \
  root@192.168.123.99:/mnt/data/ov9281-fpn/
```

`fpn_analyze.py` 用前一半帧计算、后一半帧做留出验证。暗场输出
`camN_lut.f32` / `camN_lut.txt`；平场先扣除同增益暗场，再用 33 列局部基线
生成 `camN_flat.f32` / `camN_flat.txt`，并输出条纹降低量和对比 PNG。
`fpn_pack.py` 要求暗场与平场的增益集合、尺寸完全一致。
至少需要两个增益点；建议覆盖实际会用到的最低和最高模拟增益。

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
  bin/tide_cam_fpn_calib
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
./tide_cam_capture --frames 5 --jpeg 90  # 保存首组 cam0.jpg / cam1.jpg
./tide_cam_capture --frames 0  # Ctrl-C 后在主线程正常关闭
```

`--fps` 只在 `--free-run` 下有效，参数顺序不限。在默认同步模式下传入 `--fps` 会直接报错。

验证记录与测试命令见 [VALIDATION.md](VALIDATION.md)。

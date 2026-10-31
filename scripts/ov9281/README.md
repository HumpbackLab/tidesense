# TideSense OV9281 模式与测试

左相机 `0x10` / 2 lane，右相机 `0x60` / 1 lane。SDK 配置文件为
`/mnt/data/sensor_cfg.ini`；通用采集应用使用 [tide_cam](../../tide_cam/README.md)。

| SDK 模式名 | 尺寸 | 取景 |
| --- | --- | --- |
| `OV_OV9281_MIPI_400P_20FPS_10BIT` | 640×400 | 全视场子采样，默认 |
| `OV_OV9281_MIPI_400P_60FPS_10BIT` | 640×400 | 全视场子采样 |
| `OV_OV9281_MIPI_480P_60FPS_10BIT` | 640×480 | 中央裁剪 |
| `OV_OV9281_MIPI_800P_60FPS_10BIT` | 1280×800 | 全分辨率 |

硬件同步 `hw_sync=1` 等待 FSIN；名称中的 60FPS 不代表支持 60 Hz 外部触发。
当前 FSIN 由 SC7U22 200 Hz INT1 每 10 次中断触发一次，约 20 Hz，脉宽约 100 µs。
自由运行使用 `hw_sync=0`；改变尺寸/裁剪需使用匹配的标定。

## 构建与部署（电脑）

从工程根目录执行，先有 SDK 构建依赖：

```sh
scripts/ov9281/build.sh
ssh root@192.168.123.99 'mkdir -p /mnt/data/ov9281-update'
scp -O install/ov9281-test/capture install/ov9281-test/mode_check \
  install/ov9281-test/fsin.sh root@192.168.123.99:/mnt/data/ov9281-update/
```

默认镜像只预装此目录的 `fsin.sh`，独立测试程序需另行部署。
更换内核/板级 DT 时，应同时部署匹配模块及新版脚本。

## 双目测试（板端）

停止其他相机程序后执行：

```sh
mkdir -p /tmp/ov9281-test
cd /tmp/ov9281-test
/mnt/data/ov9281-update/mode_check
/mnt/data/ov9281-update/capture /mnt/data/sensor_cfg.ini --fsin-test
```

`capture` 每路读取 600 帧，检查尺寸、时间戳、寄存器与 VI/MIPI 状态，保存两张 PGM。
`mode_check` 仅检查驱动回调，不执行传感器 I/O。
其他尺寸可用临时 INI 测试，例如自由运行 640×480：

```sh
sed s/400P_20FPS/480P_60FPS/g /mnt/data/sensor_cfg.ini > /tmp/sensor-480.ini
/mnt/data/ov9281-update/capture /tmp/sensor-480.ini --free-run
```

将替换值改为 `800P_60FPS` 可测试 1280×800。`--free-run` 只影响本次运行，不改原 INI。

## 自定义应用的 FSIN 顺序

```sh
/mnt/data/ov9281-update/fsin.sh prepare
# 在此初始化两路相机，hw_sync=1
/mnt/data/ov9281-update/fsin.sh start 20
/mnt/data/ov9281-update/fsin.sh status
# 退出前先停触发，再关闭相机
/mnt/data/ov9281-update/fsin.sh stop
```

目前只支持固定 10 分频的 20 Hz；`train 20 604` 可发送有限脉冲，结束后仍需 stop。
GPIO 由内核独占，不要与 PWM 或另一主动驱动源共用。
`fsin_diagnostics` 记录软件触发路径；VI 到达时间差不能当成曝光不同步。
寄存器背景、曝光压力测试及各模式上板记录见 [VALIDATION.md](VALIDATION.md)。

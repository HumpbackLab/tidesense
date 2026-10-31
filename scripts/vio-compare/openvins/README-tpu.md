# OpenVINS C906＋TPU LK

A53 构建灰度金字塔和维护特征，C906＋TPU 完成 LK 层级与迭代。
后端保持 FP64；编译支持后仍需运行时启用。完整像素前端卸载见
[README-frontend.md](README-frontend.md)。

## 构建

先构建 TideSense SDK、SchurVINS 静态依赖并同步 OpenVINS，在工程根目录执行：

```sh
scripts/c906/build.sh --lk-probe
python3 scripts/vio-compare/openvins/build_tpu.py --arch a53
python3 scripts/vio-compare/openvins/build_live.py \
  --core-build install/openvins-c906-tpu/build-a53 \
  --output install/openvins-c906-tpu/live/bin/openvins_live --micro-ros on
```

使用默认 16 点批次，应用与固件必须匹配。部署 `openvins_live.stripped` 为
`live/bin/openvins_live`；配置转换见 [实时说明](README-live.md)。

## 设备运行

停止其他摄像头和 TPU 用户后，在已部署目录执行：

```sh
cd /mnt/data/openvins-c906-tpu/live
OPENVINS_MICRO_ROS=0 ./with-tpu.sh ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose.csv 0

# 同步卸载对照
OPENVINS_TPU_PIPELINE=0 OPENVINS_MICRO_ROS=0 ./with-tpu.sh ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-sync.csv 0

# CPU 对照
OPENVINS_TPU_LK=0 OPENVINS_MICRO_ROS=0 ./run-live.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-cpu.csv 0
```

`with-tpu.sh` 临时加载兼容小核固件、独占接管 TPU，退出后恢复普通固件及
Linux TPU 驱动。工具默认位于 `/mnt/data/c906-reload`，可用 `C906_TOOLS_DIR` 指定。
不要同时运行两份卸载程序。设 `OPENVINS_MICRO_ROS=1` 可发布位姿，Agent 与订阅
见 [实时说明](README-live.md)。

流水线默认保留一帧前瞻，20 Hz 时增加约 50 ms 延迟；输入仍使用 latest-pair，
位姿时间戳对应当前处理帧。日志应显示 `tpu_lk=1 lookahead_pipeline=1`。
CSV 的 `sequence`、`frame_age_ms` 用于检查跳帧与图像年龄。

## 离线回放

```text
run_record CONFIG RECORD_DIR POSES.csv FRAME_LIMIT [RIGHT_ROTATION]
```

输入包含 `stereo.csv`、`imu.csv` 和相对路径图像；IMU 使用匹配配置的 SI 输入域。
右图默认旋转 180°，UZH-FPV 应传 `RIGHT_ROTATION=0`，帧数 0 表示整段：

```sh
cd /mnt/data/openvins-c906-tpu
./live/with-tpu.sh ./run_tpu_record.a53 \
  config-fpv/estimator_config.yaml dataset replay.csv 0 0
```

吞吐、等待时序、CSV 计时含义和历史对照见 [VALIDATION.md](VALIDATION.md)。

# TideSense VIO 对比工具

为 VINS-Mono、VINS-Fusion、OpenVINS 提供 A53 无 ROS 回放适配，另有主机
ov_SchurVINS 对照。上游版本锁定于 [sources.lock](sources.lock)，
源码、依赖、数据及结果生成在忽略的 `install/vio-compare-20260924-121320/`。

## 构建

先运行 `scripts/schurvins/build.sh` 准备静态 OpenCV/glog；比较所需 Ceres、
Boost stage 也需就绪。然后在工程根目录执行：

```sh
scripts/vio-compare/sync.sh
scripts/vio-compare/build.sh mono
scripts/vio-compare/build.sh fusion
scripts/vio-compare/build.sh openvins
scripts/vio-compare/build.sh ov-schurvins-host
```

`sync.sh` 获取锁定版本并应用维护补丁，拒绝修改版本不符的源码。
默认 `JOBS=2`，交叉工具链为 GCC 11.3 AArch64。适配代码保留各上游许可。

## 标定与回放

VINS-Mono 当前参数写在 `standalone/main.cpp`、`parameters.cpp`，更改后需重建。
其他适配器可从 Kalibr 生成配置：

```sh
work=install/vio-compare-20260924-121320
kalibr=cali/tidesense-imucam3-kalibr-20260924-101247
python3 "$work/vins-fusion/generate_config.py" \
  "$kalibr/imucam-camchain-imucam.yaml" "$kalibr/imucam-imu.yaml" \
  "$work/vins-fusion/rotated" --rotate-right \
  --board-output-path /mnt/data/vio-compare/vins-fusion/rotated
python3 "$work/openvins/make_config.py" --kalibr-dir "$kalibr"
```

TideSense 右相机倒装 180°；Fusion/OpenVINS 回放会旋转图像并使用匹配的虚拟相机参数。
历史录像可能已有时间或 IMU 预处理，使用配套配置，避免重复补偿。

## 入口与默认值

- [OpenVINS 实时与 ROS 2 位姿](openvins/README-live.md)、[离线回放](openvins/README.md)
- [C906＋TPU LK](openvins/README-tpu.md)、[完整像素前端](openvins/README-frontend.md)
- [ov_SchurVINS 主机对照](ov-schurvins/README.md)
- [实验探针](probes/)、[历史性能与硬件可行性分析](VALIDATION.md)

OpenVINS 默认 FP64、16×15 LK、失败点过滤。
`OPENVINS_LK_WINDOW_16X15=OFF` / `OPENVINS_LK_FILTER_VALID=OFF` 可在构建时关闭对应优化。
离线 `OPENVINS_SKIP_ACTIVE_VIZ=ON` 可跳过适配器不使用的可视化工作，默认 OFF。
FP32、RANSAC 迭代上限等实验需显式启用，详见各探针 README。

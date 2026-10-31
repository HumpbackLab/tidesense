# RV1103 OpenVINS 回放与 perf

独立 RV1103 / Cortex-A7 实验，非 TideSense SG2002 实时模式。默认保留 FP64。

## 构建与运行

本目录 `build.sh` 隔离构建回放程序、内核和静态 perf，并检查 OEM 模块 ABI。
`prepare-dataset.py` 将输入 PNG 无损转为 PGM。
固件打包使用 `build-image.sh` 和 `mkfs-images.sh`，产物为 Rockchip `update.img`，
不是整卡 SD 裸镜像。内核与模块必须整包匹配，不能只替换 boot。

本机产物：`install/rv1103-openvins-20261025/`。
已部署的板端目录：`/root/openvins-profile-20261025/`，设备 `172.32.0.93`。
在设备执行：

```sh
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
sh /root/openvins-profile-20261025/run-board.sh baseline baseline
perf stat -e cycles,instructions -- sleep 1
sh /root/openvins-profile-20261025/run-board.sh perf perf
```

先验证 PMU 计数，再采样回放。程序不自动运行。
`analyze.py` 只统计 measured=1 的帧；父阶段与子阶段耗时不能重复相加。

## 报告

- [最终耗时与热点](RESULTS.md)
- [RKIVE](RKIVE.md)、[轻量配置与长尾](LITE.md)
- [ARM32 后端](BACKEND.md)、[ov-SchurVINS](SCHUR.md)、[Schur FP32](SCHUR-FP32.md)
- [内核故障分析、旧基线与镜像验证](VALIDATION.md)

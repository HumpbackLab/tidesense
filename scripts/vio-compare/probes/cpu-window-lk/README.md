# 16／32 点 CPU 窗口 LK 微测试

在 TideSense A53 1 GHz 上测一小批完整时序 LK 的成本，用来评估前端尾部任务
能否由 A53 在后端结束后领取。没有接入任务分流，也没有测试逐相机提前发布几何。
现有 OpenVINS、C906 固件及启动配置均未修改。

## 复现

```sh
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch host
install/openvins-c906-frontend/cpu-window-lk/window_lk-host --selftest
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch host --sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  install/openvins-c906-frontend/cpu-window-lk/window_lk-host-asan --selftest
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch a53
```

设备实验目录 `/mnt/data/openvins-c906-front/cpu-window-lk`。部署
`window_lk-a53` 后使用已验证的生产实验固件，独占 TPU 运行：

```sh
exp=/mnt/data/openvins-c906-front/cpu-window-lk
mkdir -p "$exp/results"
C906_TOOLS_DIR=/mnt/data/openvins-c906-front/tools OPENVINS_TPU_FRONTEND=1 \
  /mnt/data/openvins-c906-front/live/with-tpu.sh "$exp/window_lk-a53" \
  /mnt/data/openvins-c906-tpu/dataset "$exp/results" \
  200,450,700,1000,1020,1050,1450,1940 15 --tpu
```

退出后恢复普通小核固件和 Linux TPU 驱动。省略 `--tpu` 可只测 CPU（A53
版本仍提供 ION 共享灰度对照），不需要接管 TPU。先创建输出目录。

抓取 `results/` 后汇总：

```sh
python3 scripts/vio-compare/probes/cpu-window-lk/analyze.py \
  install/openvins-c906-frontend/cpu-window-lk/results
```

`timings.csv` 保存每次调用，`points.csv` 保存 CPU／TPU 点级结果；
`summary.json` 同时包含头尾分组、图像间隔、单次 P95／最大值，以及最慢案例
的重复中位数。大差异点另外保存在 `outliers.json`。

测量复用已有五级灰度金字塔，包含窗口 Scharr 和完整 LK，不含原图预处理。
CPU/TPU 坐标不保证等价；微测试不代表混合分流后的 VIO 精度。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

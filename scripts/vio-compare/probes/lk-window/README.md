# A53 LK 窗口比较

比较灰度 LK 的 `15×15`、`16×15`、`16×16` 窗口，保留同一份 OpenCV
3.4.20 静态库和同一套 UZH-FPV `indoor_forward_6` 配置。

## 隔离方式

`build.py` 复制 `TrackKLT.cpp/.h`，只替换静态库里的 `TrackKLT.cpp.o`。
窗口赋值位于 `feed_new_camera()` 的入口、金字塔构建之前；构造函数内联在
其他编译单元，因此不能只修改头文件里的默认值。

实验代码受 `OPENVINS_LK_WINDOW_EXPERIMENT` 控制，尺寸由
`OPENVINS_LK_WINDOW_WIDTH/HEIGHT` 指定。未启用宏时保持原逻辑。
首帧日志打印实际窗口尺寸。原始源码、库、设备上的原运行程序均不覆盖。
15×15 基线的非计时输出已与之前的局部 FP32 回放逐行核对一致。
实验入口会在首帧预处理前覆盖正式默认窗口，因此仍能生成三个比较版本。

## 复现

需要先有 `backend-f32/build_variant.sh a53 mixed` 的隔离构建产物：

```bash
python3 scripts/vio-compare/probes/lk-window/build.py --precision mixed
python3 scripts/vio-compare/probes/lk-window/build_pairs.py
python3 scripts/vio-compare/probes/backend-f32/prepare_configs.py \
  install/openvins-backend-f32/perf-uzh/config-uzh-perf \
  install/openvins-lk-window/mixed --name window \
  --runtime-root /mnt/data/vio-compare/lk-window-mixed \
  --variants 15x15-r1 16x15-r1 16x16-r1 16x16-r2 16x15-r2 15x15-r2
```

把三个 `run_fisheye.*`、六个配置目录及 `run_a53.sh` 部署到设备的
`/mnt/data/vio-compare/lk-window-mixed/`。设备数据复用
`/mnt/data/vio-compare/backend-f32/uzh-input/`。

```sh
sh run_a53.sh /mnt/data/vio-compare/lk-window-mixed
flock /mnt/data/vio-compare/run.lock ./lk_pairs \
  /mnt/data/vio-compare/backend-f32/uzh-input > results-window/lk-pairs.csv
```

第二条命令需要先部署 `lk_pairs`。取回 `results-window` 后运行：

```bash
python3 scripts/vio-compare/probes/lk-window/summarize.py \
  install/openvins-lk-window/mixed/results-window \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv
```

输出 `comparison.json` 和 `summary.json`；汇总脚本也核实同一窗口两轮的
非计时轨迹和数值诊断字段完全一致。

正式构建默认 16×15，`OPENVINS_LK_WINDOW_16X15=OFF` 可恢复 15×15。
上述复现是历史局部 FP32 对照，不是当前默认 FP64 的测量。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

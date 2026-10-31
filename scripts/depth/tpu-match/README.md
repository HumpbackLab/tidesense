# 传统匹配 TPU 实验：精确整数 SAD 与 WTA

已在 SG2002 / CV181x TPU 上运行像素绝对差、15×15 SAD 窗口求和及
winner-takes-all（WTA）最佳整数视差选择。输入是 CPU XSobel cap31 预滤波后的双目灰度图，
数值范围 0..62；TPU 全程使用整数运算，不依赖神经网络、量化校准或模型权重。

本目录是独立实验，不参与 `build_all`，未替换正式深度模式或修改共享采集 helper。
正式模式继续使用已验证的 CPU all64 路径。

## 复现

电脑端从 SDK 根目录构建。需要先有既有的 A53 OpenCV/all64 依赖，以及
`install/soc_sg2002_tidesense_arm64_sd/tpu_64bit/cvitek_tpu_sdk` 的 runtime/kernel 静态库。

```sh
scripts/depth/tpu-match/build.sh
```

生成 `install/depth-tpu-match/tpu_sad_probe`，为全静态 AArch64 ELF。
未剥离程序在同目录 `build/` 下。复制到板上独立目录后运行：

```sh
./tpu_sad_probe --width 640 --height 400 --disparities 64 \
  --case plane --runs 10 --csv plane.csv
./tpu_sad_probe --width 640 --height 400 --disparities 64 \
  --case real --reference 640-ref.pgm --target 640-target.pgm \
  --aggregation tree --runs 20 --csv final-tree-640-real.csv
```

支持 `--case random|plane|flat|extreme|occlusion|real`、`--aggregation direct|tree`。
`--tile-width` / `--tile-height` 可研究分块；过大时会超出本地存储。
默认 128×64 / D16，视差范围允许 1..64。

完整检查使用 `run-board.sh`，需先准备 `320-ref.pgm` / `320-target.pgm` 和
`640-ref.pgm` / `640-target.pgm`。本次分别来自：

```text
install/stereo-bm-a53/results/320x200-32-15-150-rect[01].pgm
install/stereo-bm-a53/results/640x400-64-15-150-rect[01].pgm
```

将板端结果复制回电脑后汇总：

```sh
python3 scripts/depth/tpu-match/analyze.py RESULTS_DIR
```

核心仅包含整数 SAD/WTA，未包含完整 StereoBM 的亚像素、唯一性和过滤。
不能直接作为正式深度输出。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

# TideSense A4 双目标定板

打印文件：[tidesense-aprilgrid-a4-6x4-32mm.pdf](tidesense-aprilgrid-a4-6x4-32mm.pdf)。
Kalibr 配置：[tidesense-aprilgrid-a4-6x4-32mm.yaml](tidesense-aprilgrid-a4-6x4-32mm.yaml)。
PNG 仅用于预览，请打印 PDF。

## 规格

- A4 横向，297 × 210 mm，单页矢量图。
- AprilGrid：6 列 × 4 行，tag36h11，ID 0–23。
- 使用 Kalibr 官方生成器的两格黑边和对称角点黑方块。
- ID 0 在左下角；同一行向右递增，再向上一行。
- 单个标签黑色外边缘之间的边长：32 mm；标签间距：9.6 mm。
- `tagSize: 0.032`（米），`tagSpacing: 0.3`（9.6 / 32，无单位）。
- 标签阵列外边缘范围：240 × 156.8 mm；包含外围角点黑方块：259.2 × 176 mm。
- 图案居中，左右留白 18.9 mm，上下留白 17 mm；文字和尺标位于留白内。

## 打印与使用

1. 选择 **A4、横向、实际大小 / 100%**。关闭“适合页面”、缩放、海报拼接。
2. 使用清晰黑白打印，贴在平整、坚硬的哑光板上，避免翘曲、气泡和反光覆膜。
3. 打印后量取底部两刻线间距，应为 **100 mm**；再检查标签宽、高均为 **32 mm**。
   长距离复核可测整排标签的左右黑边，长度应为 **240 mm**。
4. 如果存在均匀缩放，应重新按原尺寸打印，或把 YAML 的 `tagSize` 改为实测米数；
   等比缩放时 `tagSpacing` 仍为 0.3。若横纵比例不同或纸张不平，请重新制作。
5. 两颗 OV9281 共用这一张板。用实际 VIO 所采用的分辨率、裁剪及固定镜头设置采集标定数据。
   让左右相机都有清晰可辨的共同标签，并采集不同位置、倾角和距离，覆盖画面中央与边缘。
   A4 的可用距离取决于镜头视场、对焦和图像中标签像素数，不能仅凭 50 mm 基线确定。
6. Kalibr 命令中设置 `--target` 为上述 YAML。采集场景中隐藏其他 AprilTag。

## 来源、重建与验证

编码、位顺序、黑边及角点布局依据 [Kalibr 官方生成器](https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/aslam_offline_calibration/kalibr/python/kalibr_create_target_pdf)。
参数定义见 [Kalibr calibration targets](https://github.com/ethz-asl/kalibr/wiki/calibration-targets)。
本目录脚本调整了 A4 页面布局，并加入尺寸核对尺。

安装 PyMuPDF 后可重建：

```sh
python3 scripts/calibration/generate_aprilgrid_a4.py
```

已验证：PDF 为单页 A4 横向；300 dpi 渲染后，OpenCV 4.10 的 AprilTag 36h11
检测器（`markerBorderBits=2`）识别出全部 24 个 ID，且位置与 YAML 的行列顺序一致。
这验证了电子文件，不代表已验证实际打印误差或实拍标定精度。

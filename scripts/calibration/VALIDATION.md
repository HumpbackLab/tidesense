# calibration 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## AprilGrid 尺寸与生成依据

- A4 横向，297×210 mm，打印选择实际大小 / 100%，关闭适合页面或缩放。
- tag36h11，6 列×4 行，ID 0–23；ID 0 在左下角，同一行向右递增，再向上一行。
- 标签外黑边边长 32 mm，间距 9.6 mm；`tagSize: 0.032`，`tagSpacing: 0.3`。
- 标签阵列外边缘 240×156.8 mm，包含外围角点黑块为 259.2×176 mm。
- 将图案贴在平整、坚硬的哑光板上，避免翘曲、反光或手遮挡。
- 打印后核对底部 100 mm 尺标和标签宽高。等比缩放时应重印或按实测尺寸修正
  `tagSize`；横纵比例不一致时重新制作。

### 来源与重建

AprilGrid 编码和布局依据 [Kalibr 目标生成器](https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/aslam_offline_calibration/kalibr/python/kalibr_create_target_pdf)。
参考 [标定板说明](https://github.com/ethz-asl/kalibr/wiki/calibration-targets)、
[相机–IMU 标定](https://github.com/ethz-asl/kalibr/wiki/Camera-IMU-calibration)、
[IMU 噪声模型](https://github.com/ethz-asl/kalibr/wiki/IMU-Noise-Model)。
安装 PyMuPDF 后可执行 `python3 scripts/calibration/generate_aprilgrid_a4.py` 重建打印文件。

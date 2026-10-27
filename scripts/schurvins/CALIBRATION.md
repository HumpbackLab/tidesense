# 标定数据录制

标定直接复用 `tidesense_record`，与数据集使用相同的原始图像、IMU 和时间戳格式。
无需启动 VIO、micro-ROS Agent 或单独的标定采集程序。

板端：

```sh
cd /mnt/data/tidesense-dataset
./run.sh --output /mnt/data/calib-motion --seconds 120
```

输出目录必须不存在。首次标定不传 `--calibration`；默认双目 640×400，
可通过 `--width` / `--height` 选择支持的尺寸。采集期间固定 AprilGrid，移动整个设备，
覆盖不同位置、距离、倾角以及三个轴的旋转和平移。以实际角点与运动激励判断数据是否充足。

电脑端复制数据后：

```sh
python scripts/dataset/export.py cali/calib-motion cali/calib-export --time-offset-ns 0 --bag
scripts/calibration/kalibr.sh cali/calib-export/dataset.bag
```

导出依赖安装、相机–IMU 联合求解与配置部署见 [完整标定流程](../calibration/README.md)。
旧 `--calibrate` ROS 图像发布入口已从 VIO 程序移除；历史验证数据仍保留在
[CALIBRATION-VALIDATION.md](CALIBRATION-VALIDATION.md)。

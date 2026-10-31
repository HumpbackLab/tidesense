# TideSense StereoBM A53 benchmark

Independent stereo-depth benchmark: no VIO, IMU processing, ROS, IVE or TPU.
Uses the existing static OpenCV 3.4.20 libraries with the NEON CPU baseline,
GCC 11.3 `-O3 -mcpu=cortex-a53`, one OpenCV thread.

## Build and prepare

From the SDK root:

```sh
bash scripts/vio-compare/probes/stereo-bm/build.sh
/usr/bin/python3 -S - <<'PY'
import sys, runpy
sys.path.insert(0, '/usr/lib/python3/dist-packages')
sys.argv = ['prepare.py',
    'cali/tidesense-stereo-yy-equi-20261011-231002/stereo-camchain.yaml',
    'install/stereo-bm-a53']
runpy.run_path('scripts/vio-compare/probes/stereo-bm/prepare.py', run_name='__main__')
PY
```

The map generator uses the original raw-image Kalibr pinhole/equidistant
calibration, including the right camera's 180 degree installation. It computes
fisheye stereo rectification at 320x200 and 640x400 with `balance=0`. Right-image
rotation is folded into the fixed-point remap. Both raw inputs remain 640x400.
The calibrated baseline is 49.667 mm. Rectified focal length is 139.987 px at
320x200 or 279.974 px at 640x400.

For positive disparities this rig requires using the physical right rectified
view as the matcher reference. `rect0` and the depth images therefore refer to
the rectified physical right camera; `rect1` is the rectified physical left.
These labels do not change the dataset's left/right files.

Copy `stereo_bm`, `maps-*.bin`, `maps.json` and `run-board.sh` to a board directory.
Run on the board:

```sh
sh /mnt/data/stereo-bm-bench-20261012/run-board.sh \
  /mnt/data/stereo-bm-bench-20261012 \
  /mnt/data/openvins-imu-bench-20261012-084638/input \
  > /mnt/data/stereo-bm-bench-20261012/board-run.log 2>&1
```

Input is the existing lossless PGM export of `dataset-20261012-084638`, with
885 synchronized pairs. No frames are dropped or repeated. The shell runs three
configurations: 320x200/32 disparities, 640x400/64, 320x200/64, all with block 15.

## 输出与实验

计时 CSV 分别记录读取、校正、BM、毫米深度转换和整体处理；不含采集或 ROS 发布。
深度 PNG 为 uint16 毫米，0 表示无效。`rect0` 是物理右相机校正视图，
用于正视差匹配；不改原始左右文件含义。

`build-opt.py` 生成独立的 stripe/wide/recompute 实验，默认均关闭；
`profile-board.sh` 用于硬件 perf 采样。完整参数、计时口径与复测见
[VALIDATION.md](VALIDATION.md)，既有报告保留在 [results/](results/)。

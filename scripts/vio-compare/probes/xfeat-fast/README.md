# FAST / XFeat 前端实验

本机稀疏描述子对照，以及 SG2002 TPU 完整 XFeat/OpenVINS 回放实验。
不接入默认构建；本机匹配数不代表轨迹精度，TPU 推理时间不代表整帧耗时。

## 本机匹配对照

需主机 PyTorch/OpenCV 环境，在工程根目录执行：

```sh
git clone https://github.com/verlab/accelerated_features.git /tmp/tidesense-xfeat
git -C /tmp/tidesense-xfeat checkout e92685f57f8318b18725c5c8c0bd28c7fe188d9a
python3 -m venv --system-site-packages /tmp/tidesense-xfeat-venv
/tmp/tidesense-xfeat-venv/bin/pip install 'numpy<2'
/tmp/tidesense-xfeat-venv/bin/python scripts/vio-compare/probes/xfeat-fast/compare.py \
  --replay install/dataset-analysis-20260924-174756/replay \
  --xfeat /tmp/tidesense-xfeat \
  --out install/openvins-pipeline-20260925/xfeat-fast-tidesense-070.json \
  --pairs-per-class 20 --threshold 0.70
```

The upstream checkout includes `weights/xfeat.pt`. The output JSON has one
record per camera-pair observation and includes the individual timing and
count measurements. The UZH-FPV run uses
`install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay`
as `--replay`.

## TPU 与完整回放

- `export_tpu.py`：导出描述子网络，加 `--with-heads` 导出完整检测头。
- `make_calibration.py`：准备 INT8 校准输入；后续用 TPU-MLIR 为 cv181x 编译。
- `XFeatTPU.cpp/.h`、`track-descriptor.patch`、`xfeat-cmake.patch`：OpenVINS 集成。
- 运行变量 `OPENVINS_XFEAT_MODEL` 指定模型；不设置时使用原 FAST/ORB。
- `OPENVINS_XFEAT_IMU_INIT=1` 仅用于带独立初始化补丁的历史对照。

TPU 导出、依赖路径、部署命令和完整轨迹结果见 [VALIDATION.md](VALIDATION.md)。
当前完整集成实验未表现出替代默认 KLT 的速度或精度收益。

# OpenVINS 后端精度实验

**常规测试和实时部署使用 FP64。** 局部 FP32 与完整 FP32 都是显式选择的实验，
在部分强运动/实时输入上出现过负协方差，不能依据单段成功回放默认启用。

## 构建

先准备 SDK 的 Boost/OpenCV/Ceres 和已有鱼眼回放适配器，在工程根目录执行：

```sh
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 mixed
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp32
# 本机对照，将 a53 换为 host
scripts/vio-compare/probes/backend-f32/build_variant.sh host fp64
```

省略精度参数默认为 `fp64`。独立源码和产物位于
`install/openvins-backend-f32/{a53,host}-{fp64,mixed,fp32}/build/`，不替换实时程序。
脚本依赖 `install/vio-compare-20260924-121320/openvins/open_vins` 的 Git HEAD 和
`install/fisheye-benchmark/openvins/run_fisheye.cpp`。

- `mixed`：仅部分矩阵乘法使用 FP32，状态协方差保留 FP64。
- `fp32`：协方差、传播及视觉更新矩阵转换为 FP32；名义状态、时间戳和相机接口保留 FP64。
- 完整 FP32 需库与调用程序使用相同宏/头文件，不能混用 FP64 ABI。
- 默认开启 16×15 LK 和失败点过滤；复现旧 15×15 测量需设置
  `OPENVINS_LK_WINDOW_16X15=OFF` 后重建。

## 板端回放与检查

复制 A53 程序及配置到设备后执行：

```sh
OPENVINS_NUMERIC_EIGEN=1 OPENVINS_FP32_PACKED_MA=1 \
  ./run_record config/estimator_config.yaml record_dir trajectory.csv 0
```

`OPENVINS_FP32_PACKED_MA=1` 只切换完整 FP32 的打包乘法；
`OPENVINS_NUMERIC_EIGEN=1` 启用额外特征值诊断，不应计入正常性能比较。
各模式准备和批量比较：

```sh
sh run_a53.sh /mnt/data/vio-compare/backend-f32 mild \
  /mnt/data/vio-record-20260924-121320 744 'original mixed fp32 fp32packed'
```

上述脚本需先部署各变体和对应配置，取回结果后运行：

```sh
python3 scripts/vio-compare/probes/backend-f32/compare.py \
  install/openvins-backend-f32/board-results/results-mild \
  --variants original mixed fp32 fp32packed
```

数值故障、历史平台/数据集对照见 [VALIDATION.md](VALIDATION.md)，
热点采样见 [PERF.md](PERF.md)。失败运行的均值不能作为完整回放成绩。

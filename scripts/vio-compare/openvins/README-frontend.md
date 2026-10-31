# OpenVINS 完整像素前端卸载

该实验将直方图均衡、五级灰度金字塔、FAST/NMS、网格补点、亚像素角点、
补点左右匹配及左右时序 LK 迁至 C906＋TPU。C906 负责流程、检测和收敛，
TPU 负责整数 LUT、Gaussian 及 LK 窗口计算。A53 保留去畸变、RANSAC、
特征库维护和原始 FP64 后端。后端没有启用完整或局部 FP32。

## 开关与通信

编译宏 `OPENVINS_C906_FRONTEND` 默认 OFF，依赖 `OPENVINS_C906_TPU_LK`。
即使编译支持，普通 `run-live.sh` 仍默认 CPU；运行时同时设置
`OPENVINS_TPU_LK=1 OPENVINS_TPU_FRONTEND=1` 才启用完整卸载。
`OPENVINS_TPU_PIPELINE=0` 可关闭前瞻流水线，保留同步完整卸载。

`OPENVINS_C906_FRONTEND_EARLY` 是独立的提前预处理编译开关，默认 OFF；
`build_tpu.py --frontend --early-pipeline` 启用；运行时设
`OPENVINS_TPU_FRONT_EARLY=0` 可关闭提前预处理。

每帧一次邮箱请求，C906 缓存前后帧金字塔；A53 后端与下一帧前端重叠。
提前预处理需配对更新应用和固件。支持 640×400 / 640×480 mono8、
NONE/HISTOGRAM 均衡；生产配置为总计 150 点、16×15 LK、失败点过滤。
每相机目标最多 120 点，不支持 CLAHE；输入相机顺序须为 0、1。

## 构建

```sh
C906_FRONT_FASTMEM=ON C906_LK_DUAL_BATCH=ON scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/openvins/build_tpu.py --frontend --early-pipeline --sparse-nms --lazy-mask --latest-intrinsics --lk-batch 16 --lk-roi-cache --lk-dual --qr7 --arch a53 --jobs 4
python3 scripts/vio-compare/openvins/build_live.py \
  --core-build install/openvins-c906-tpu/build-a53 \
  --output install/openvins-c906-frontend/live/bin/openvins_live --micro-ros on
```

固件和程序必须配套部署。独立构建不改 BOOT 分区或默认开机固件。

## 设备使用

独立目录 `/mnt/data/openvins-c906-front`，`live/config` 沿用设备已转换的
TideSense 标定及 IMU 修正。停止其他摄像头/TPU 用户后：

```sh
cd /mnt/data/openvins-c906-front/live
OPENVINS_MICRO_ROS=0 ./run-front.sh \
  config/estimator_config.yaml /mnt/data/sensor_cfg.ini auto pose-front.csv 0
```

需要 ROS 位姿时改为 `OPENVINS_MICRO_ROS=1`，Agent 和订阅方式见
[实时说明](README-live.md)。启动日志显示 `frontend=1`；CSV `front_calls`
表示每帧一条完整请求，`front_*_ms` 区分上传 CPU 与小核各阶段耗时。
旧 `tpu_hits/calls` 属于逐组 LK 路径，完整卸载时这些计数为零是正常的。

`run-front.sh` 临时重载兼容小核固件、接管 TPU，退出后恢复普通固件和原
Linux TPU 驱动。工具位于 `../tools`，也可通过 `C906_TOOLS_DIR` 指定。
直接运行 `run-live.sh` 且不设置 TPU 环境变量即可使用原 CPU 路径。
不要同时运行两份卸载程序。

## 可选 IVE

Hist 由 IVE 计算直方图，C906 生成 LUT，TPU 应用 LUT；ST 用于替换 FAST 候选。
这些开关默认关闭，支持 640×400 / 640×480；ST 会改变点集。

| 功能 | build_tpu.py 参数 | 固件构建变量 | 运行变量 |
| --- | --- | --- | --- |
| 对齐布局 | `--aligned-images` | `C906_FRONT_ALIGNED_IMAGES=ON` | 编译时决定 |
| Hist | `--ive-hist` | `C906_FRONT_IVE_HIST=ON` | `OPENVINS_TPU_IVE_HIST=1` |
| ST | `--ive-st` | `C906_FRONT_IVE_ST=ON` | `OPENVINS_TPU_IVE_ST=1` |

IVE 运行还需 `OPENVINS_TPU_IVE=1`，由包装器独占接管引擎。
ST 质量阈值 `OPENVINS_TPU_IVE_QUALITY` 默认为 16，与 FAST 阈值含义不同。
独立探针见 [c906-ive](../probes/c906-ive/README.md)。
完整构建命令、性能/精度对照和历史优化见 [VALIDATION.md](VALIDATION.md)。

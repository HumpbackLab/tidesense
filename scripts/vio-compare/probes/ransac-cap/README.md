# RANSAC 最大迭代数 200（可选）

仅保留 `OPENVINS_RANSAC_CAP_200` 开关，默认 OFF。

- ON：基础矩阵 RANSAC 最大 200 次。
- OFF：原调用，OpenCV 默认最大 1000 次。
- 置信度保持 0.999，几何阈值保持 `2.0 / max_focallength`。
- LK 收敛阈值保持 0.01 px、最大迭代数 30，沿用既有 16×15 窗口和失败点过滤。

## 启用和关闭

这是编译开关，不是已编译程序的运行时环境变量。

```sh
OPENVINS_RANSAC_CAP_200=ON scripts/vio-compare/build.sh openvins
OPENVINS_RANSAC_CAP_200=OFF scripts/vio-compare/build.sh openvins

# 原始 FP64 后端的独立构建，也支持同一开关：
OPENVINS_RANSAC_CAP_200=ON \
  scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp64
```

直接使用 CMake：`-DOPENVINS_RANSAC_CAP_200=ON`，生成同名 0/1 宏。
补丁由标准和独立构建流程自动应用。单目时域匹配和双目匹配共用此几何筛选入口。

## 实验依据

2026-10-13，A53 单核 1 GHz，单目 150 点、纯 FP64、3 px 视觉噪声、IMU intrinsic 在线校准、无 ROS，回放 `dataset-20261012-084638`：

| 设置 | 正常整帧平均 | 折算 FPS | 首末位移 |
|---|---:|---:|---:|
| 基线（两轮） | 80.16 ms | 12.47 | 1.251 m |
| 最大 200 次 | 58.33 ms | 17.14 | 1.497 m |

本机单目 UZH-FPV `indoor_forward_6` 的 ATE 约 0.2387 m，两者基本一致（共同真值覆盖 816 帧，SE(3) 对齐，不拟合尺度）。
TideSense 录像无真值；首末位移只是漂移参考。最大 200 次使几何接纳点数低于 20 的调用从 55 增加到 77，故保留为可选项。

本轮其他实验源码、构建入口和设备可执行程序已撤销：置信度调整、LK 阈值调整、NEON 加载及组合开关。
历史原始数据和报告保存在 `install/openvins-mono-front/`；标准实时程序默认配置未修改。

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

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

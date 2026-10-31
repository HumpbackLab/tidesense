# LK 失败点过滤（默认启用）

当前仅保留 `OPENVINS_LK_FILTER_VALID`，默认 ON。IMU 旋转初值、浅层 LK 与完整金字塔回退的维护源码、构建脚本和运行开关已撤销。

## 实现

[openvins-lk-filter.patch](../../patches/openvins-lk-filter.patch) 修改 `TrackKLT::perform_matching`：

1. 正常执行完整金字塔 LK，窗口仍为默认 16×15。
2. 仅对 LK 成功且目标坐标有限的点进行归一化和 RANSAC。
3. 有效匹配少于原来的最低要求 10 个时全部拒绝。
4. 将 RANSAC 掩码映射回原特征索引，保留特征 ID 和输出向量长度。

RANSAC 阈值、置信度、最大迭代次数与后端观测检查保持原值。未增加陀螺积分、CameraData 字段或浅层重试。编译宏为 OFF 时直接恢复原来的归一化/RANSAC 路径。

## 构建与关闭

标准 A53 构建和 FP64/局部 FP32/完整 FP32 的 precision 构建均自动应用补丁，默认开启，无需运行时环境变量。

```sh
scripts/vio-compare/build.sh openvins

# 关闭过滤，重新编译
OPENVINS_LK_FILTER_VALID=OFF scripts/vio-compare/build.sh openvins

# 完整 FP32 的隔离构建（该后端本身仍为可选项）
scripts/vio-compare/probes/backend-f32/build_variant.sh host fp32
scripts/vio-compare/probes/backend-f32/build_variant.sh a53 fp32

# 隔离构建中关闭过滤
OPENVINS_LK_FILTER_VALID=OFF \
  scripts/vio-compare/probes/backend-f32/build_variant.sh host fp32
```

直接使用 CMake 可传入 `-DOPENVINS_LK_FILTER_VALID=OFF`。默认开启过滤不等于默认开启完整 FP32 后端。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

# lk-filter 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

### 实验依据

UZH-FPV `indoor_forward_6` 完整 1970 帧。本机过滤单项 ATE 0.2376m，A53 全 FP32 packed 两轮为 67.460/67.162ms（全段平均）、71.525ms（初始化后两轮平均），ATE 0.2108m；两轮非计时轨迹和协方差输出完全一致。

在其他条件均开启 IMU 初值的受控对照中，过滤使相机时间从 70.657 降至 66.873ms，减少 5.35%，主要来自 RANSAC。IMU 初值只得到约 0.75% 的全流程收益，浅层回退变慢约 7.65%，故撤销这两项。

历史数据见 [experiment-results.json](experiment-results.json)；[history.md](history.md) 记录原实验，里面的旧构建/运行命令已撤销。真值只覆盖中间 816 帧，ATE 使用 SE(3) 对齐和原 cam0 时间修正，不拟合尺度。

完整 FP32 的全关基线曾在板端触发负协方差退出；开启过滤后的单次/重复成功不代表该后端在其他输入上数值稳定。

### 精简并默认启用后的验证

2026-10-03 使用新的默认实现重新编译本机/A53 完整 FP32 版本，并回放全部 1970 帧。两边均输出 1782 帧初始化后位姿；全部非计时轨迹和协方差字段与旧“filter 单项”实验逐行完全一致。

| 平台 | 真值段 ATE/m | 全段相机平均 ms | 初始化后平均 ms |
|---|---:|---:|---:|
| 本机 | 0.2376 | 5.237 | 5.510 |
| A53 1 GHz | 0.2108 | 66.855 | 71.040 |

CMake `OPENVINS_LK_FILTER_VALID=OFF` 实际生成 `=0` 编译定义，tracker 的关闭路径编译检查通过。`sync.sh` 连续运行两次通过，补丁应用具备幂等性。验证摘要见 [default-verification.json](default-verification.json)。

新默认实现的隔离回放程序已部署并运行于 `/mnt/data/vio-compare/lk-filter-default/run_fisheye`；本机/板端原始结果在 `install/openvins-lk-filter/` 与对应板端目录。旧策略实验的日志保留供追溯，其维护源码和脚本已经删除。

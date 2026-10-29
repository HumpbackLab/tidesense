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
`build_tpu.py --frontend --early-pipeline` 启用。编入后，完整卸载运行时默认
提前预处理，设 `OPENVINS_TPU_FRONT_EARLY=0` 可恢复原调度作对照。

提前调度在第 N 帧收到小核结果后，立即上传 N+1 帧并启动均衡/金字塔；
A53 同时对 N 帧做去畸变、RANSAC 和特征库更新，结束后发布筛选好的点和 ID。
小核随后继续补点和 LK，A53 执行 N 帧后端。图像及点快照分开发布到不同
缓存行，但仍只发送一次邮箱请求；前端不会使用未经过几何筛选的点。
小核等点快照有 500 ms 超时，主机等待工作线程时的异常也会传出。
新协议为 v3，必须配对更新固件和应用。

`FRONT_FAST_FILL` 独立控制小核前端的对齐 64 位填充，避免 SDK 逐字节
`memset` 的开销。通过 `C906_FRONT_FASTMEM=ON` 构建启用，不替换全局
内存函数，不改变像素、特征数、角点/光流阈值或后端精度。

每帧上传两张原始灰度图、掩码和点/ID 快照，只发一次邮箱请求。小核缓存
前后两帧的双目金字塔，完成预处理、补点及三组光流后一次返回；中间无需
A53 串接 LK 请求。使用固定 12 MiB ION 缓冲区，一次仅允许一帧在途。
第 N 帧后端与第 N+1 帧像素前端重叠，消费时仍需要等待未完成的前端。
单帧前瞻会增加约一个输入周期的延迟，摄像头适配器仍使用 latest-pair 策略。

当前接口限制：按顺序输入相机 0、1；640×400 或 640×480；灰度 CV_8UC1；
NONE/HISTOGRAM 两种均衡模式，不支持 CLAHE；每相机目标最多 120 点，
网格每边最多 16 格且总格数不超过目标点数，最小间距 4–128 像素。
生产配置使用总计 150 点、16×15 LK 和失败点过滤。

## 构建

```sh
C906_FRONT_FASTMEM=ON scripts/c906/build.sh --frontend-probe
python3 scripts/vio-compare/openvins/build_tpu.py --frontend --early-pipeline --arch a53 --jobs 4
python3 scripts/vio-compare/openvins/build_live.py \
  --core-build install/openvins-c906-tpu/build-a53 \
  --output install/openvins-c906-frontend/live/bin/openvins_live --micro-ros on
```

小核程序只在实验固件中启用 C++ 像素内核，并增加命令任务栈；BOOT 分区和
正常开机固件没有改变。SDK 原 `memset()` 缺失返回值，编译器优化后的数组
初始化会使用错误指针，因此修复为返回 `dest`，并在构建中重新安装 common 库。
修复单独保存于 `scripts/c906/freertos-memset.patch`；小核适配钩子保存在
`scripts/c906/freertos-hooks.patch`，已集成的源码不需重复应用。

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

## 对照与结果

2026-10-29，A53 1 GHz、C906_0 750 MHz、TPU FAB 900/AXI 700 MHz，
UZH-FPV indoor_forward_6，1970 帧，150 点，原始 FP64 后端。

* 主机 CPU 模拟完整卸载的异步、同步回放，均与原 CPU 轨迹逐行完全一致。
* 板上 6 组真实图像的各级金字塔像素、补点坐标及 ID，与 OpenCV/原检测器
  对照一致（640×480、FAST50）。640×400/FAST20 的像素、点数和 ID 也一致；
  一个亚像素输入舍入差异为 0.0000076 px，经过 BF16 LK 后表现为 0.015 px。
  两条路径使用相同坐标重测，LK 输出差异为零；测试不会通过放宽光流阈值掩盖差异。
  硬件 LK 使用已有 BF16 窗口计算，因此不要求与 CPU 轨迹逐位一致。
* 完整板上回放初始化后 1781 帧，SE(3) 对齐、不缩放的 ATE 为 0.287 m，
  原同步 TPU-LK 为 0.297 m，原 CPU 为 0.295 m；共同真值覆盖 816 帧，
  时间平移 -16,684,572 ns。差异不宜解释为精度提升。
* 小核完整前端平均 41.05 ms、P95 65.33 ms、最慢 91.98 ms，约 30.7% 超过
  50 ms。均衡 12.14 ms、金字塔 1.95 ms、补点 14.54 ms、LK 11.96 ms。
  A53 上传约 1.29 ms CPU/帧，原部分卸载工作线程约 23.74 ms CPU/帧。
* 整段含 SD 的回放 210.21 s，原不等待首批的部分卸载为 243.99 s。
  camera 调用窗口不包括全部输入读取/前瞻工作，不能取其倒数宣称实时帧率。

相同 RAM 窗口（第 1002–1037 帧，保留原前缀初始化）对照如下：

| 指标 | 原部分卸载 | 完整像素前端卸载 |
| --- | ---: | ---: |
| 完整帧周期 | 85.98 ms | 75.26 ms |
| 吞吐 | 11.63 Hz | 13.29 Hz |
| camera 调用窗口 | 85.05 ms | 74.91 ms |
| 主线程 CPU | 47.65 ms | 45.93 ms |
| 窗口内进程 CPU | 73.79 ms | 47.51 ms |
| 消费前端结果等待 | 15.64 ms | 27.44 ms |

新版本此窗口小核前端平均 54.68 ms、P95 64.99 ms，后端 CPU 33.61 ms。
RAM 回放与同版完整 SD 回放的前 1040 帧轨迹逐行一致。帧周期下降约 12.5%，
窗口内进程 CPU 下降约 35.6%，但前端自身变慢使等待增加。

最终设备实时冒烟测试 100 帧，61 帧初始化后更新，摄像头/消费均为 19.95 Hz、
IMU 199.54 Hz、无 sequence gap，初始化后 camera 调用平均 31.31 ms。
测试场景较静止且仅约 6.1 s，
不能代表强运动或长期性能；退出后已恢复普通固件/驱动。

2026-10-29，随后在同一程序、同一配置和相同 RAM 窗口上，分别验证两项优化：

| 指标 | 原调度/普通填充 | 提前预处理 | 提前预处理＋快速填充 |
| --- | ---: | ---: | ---: |
| 完整帧周期平均 | 75.49 ms | 67.32 ms | 64.07 ms |
| 帧周期 P95 | 123.93 ms | 117.23 ms | 113.09 ms |
| 吞吐 | 13.25 Hz | 14.85 Hz | 15.61 Hz |
| A53 消费前端结果等待 | 27.75 ms | 19.48 ms | 16.15 ms |
| C906 前端实际计算，扣除等快照 | 54.80 ms | 54.89 ms | 50.67 ms |
| C906 等待点快照 | 0 | 3.08 ms | 3.54 ms |
| 直方图均衡 | 12.19 ms | 12.20 ms | 9.14 ms |
| 补点/亚像素精化 | 22.40 ms | 22.42 ms | 21.05 ms |
| LK 合计 | 17.71 ms | 17.79 ms | 17.95 ms |
| FP64 后端 wall | 35.03 ms | 33.49 ms | 33.55 ms |
| A53 主线程 CPU | 45.94 ms | 46.02 ms | 46.11 ms |

三组前 1040 帧轨迹逐行一致，也与原完整硬件回放的对应前缀一致。新调度的
主机完整 1970 帧回放与原 CPU 参考逐行一致。最终优化版在板上也完成了完整
1970 帧回放，1781 帧初始化后更新，轨迹与优化前硬件参考逐行完全一致，
每帧恰好一条请求。完整回放的前端计算平均 37.31 ms、P95 60.74 ms，
其中均衡 9.13 ms、金字塔 2.05 ms、补点 13.59 ms、LK 12.07 ms。
最终实时冒烟测试 100 帧，61 帧初始化后更新，摄像头及消费均为 19.95 Hz，
IMU 199.54 Hz、无跳帧；静止/ZUPT 场景不能代替强运动吞吐测试。最终帧周期缩短约 15.1%，吞吐
提高约 17.8%，A53 等待减少约 41.8%；后端及几何检查的数值精度没有改变。
发布点快照只消耗约 0.021 ms A53 CPU。`front_total_ms` 从新协议开始包含
`front_snapshot_wait_ms`，比较计算耗时应扣掉该等待；各阶段 P95 不能直接相加。

原始三组文件保存在 `install/openvins-c906-frontend/early-results/board/`，汇总：

```sh
python3 scripts/vio-compare/openvins/compare_front_pipeline.py \
  install/openvins-c906-frontend/early-results/board \
  --reference install/openvins-c906-frontend/front.csv
```

对照组合分别是 `OPENVINS_TPU_FRONT_EARLY=0`＋无快速填充固件、
`OPENVINS_TPU_FRONT_EARLY=1`＋无快速填充固件，以及提前调度＋快速填充固件。
无快速填充固件使用 `C906_FRONT_FASTMEM=OFF scripts/c906/build.sh --frontend-probe`
生成；默认 `--frontend-probe` 已启用快速填充，其余普通固件不受影响。

这是已验证的完整像素前端迁移，尚未保证所有帧 50 ms，也未保证稳定实时 20 Hz。
需要进一步优化 C906 补点、均衡和强运动时的 LK 尾延迟。

离线复现（右图已经符合 FPV 方向，因此最后参数为 0）：

```sh
cd /mnt/data/openvins-c906-front
C906_TOOLS_DIR="$PWD/tools" OPENVINS_TPU_FRONTEND=1 \
  ./live/with-tpu.sh ./run_front.a53 \
  /mnt/data/openvins-c906-tpu/config-fpv/estimator_config.yaml \
  /mnt/data/openvins-c906-tpu/dataset results/front.csv 0 0
```

设置 `OPENVINS_PRELOAD_FIRST=1000 OPENVINS_PRELOAD_COUNT=40`，将帧数限制设为
1040，可复现 RAM 窗口，避免 SD 读取隐藏前端等待。详细原始文件位于
`install/openvins-c906-frontend/`。像素内核带 OpenCV/FAST 许可说明，见
`probes/c906-frontend/NOTICE`，没有更改原检测和亚像素算法以换取速度。

结果汇总可重现：

```sh
python3 scripts/vio-compare/openvins/analyse_frontend.py \
  install/openvins-c906-frontend \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv \
  --baseline-memory install/openvins-c906-tpu/timing-results/memory.csv
```

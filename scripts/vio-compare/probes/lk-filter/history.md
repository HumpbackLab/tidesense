> 历史实验记录：IMU 初值、浅层与回退实现已撤销；下列旧实验构建/运行脚本已删除，不用于当前构建。当前实现见 [README.md](README.md)。

# UZH-FPV：LK 策略实验

实验日期：2026-10-03。使用 `indoor_forward_6` 完整 1970 帧，双目原始帧率约 30 Hz，IMU 500 Hz。板端 PNG 与本机 PGM 的解码像素相同。

## 隔离与对照

三个编译宏在生成源码的 CMake 中默认 OFF；实验脚本才打开三个宏。生产 SDK 的默认配置未接入这些实验。

| 宏/同名环境变量 | 行为 |
|---|---|
| `OPENVINS_LK_FILTER_VALID` | LK 后只对成功且坐标有限的匹配做归一化、RANSAC，结果映射回原来的特征索引 |
| `OPENVINS_LK_IMU_INITIAL` | 用相邻帧间陀螺积分、当前陀螺偏置、cam-IMU 时间差、相机旋转外参预测像素，作为完整金字塔 LK 初值 |
| `OPENVINS_LK_ADAPTIVE` | 需要 IMU 初值；时间跟踪先用 maxLevel=1，失败/平均灰度误差 >12/偏离预测 >8px 的点从原始预测重跑完整金字塔；RANSAC 接受比例 <60% 时整幅相机重跑 |

环境变量值必须为 `1` 才启用。关闭宏时保留原始匹配实现；已检查 tracker 与 replay adapter 在三个宏全 OFF 时能编译。新点的左右目匹配、金字塔构建仍走原路径。没有改变 RANSAC 最大迭代次数、阈值、噪声、初始化或后端观测门限。

五组：baseline（全关）、filter、imu、adaptive（imu+adaptive）、combo（三项全开）。另外补充 filterimu（过滤+IMU、不启用浅层），用于在可完整运行的组合间分别比较三个开关。窗口始终为当前默认 16×15；本机和 A53 都使用全 FP32 后端。`OPENVINS_FP32_PACKED_MA=1/0` 分别选择打包/原始分块矩阵乘法，两者均为 FP32。

IMU 预测目前在离线实验 adapter 内生成，还没有接入生产的设备实时输入。需要在现场数据上进一步验证后再接入。

## 精度和耗时口径

- 成功方案要求完整回放、1782 个初始化后输出、无非有限状态、协方差对角检查通过。
- GT 只覆盖中间 816 帧；采用 cam0 的时间修正 -16684572 ns，SE(3) 对齐，不拟合尺度。
- 精度门槛：完整回放；GT 覆盖帧与基线一致；ATE 不超过基线 + max(0.05m, 25%)；全段与基线的位置差末帧 <1m、最大 <2m。这是本次实验的相对准入门槛，不是产品精度承诺。
- 末帧与基线位置差用于检查轨迹变化，不是绝对漂移；GT 段以外只能判断是否有明显异常，不能保证绝对精度。
- `compute_ms` 包含 gyro 初值生成与 `feed_measurement_camera` 全流程，排除图片解码、CSV 写入、IMU 入队、数值诊断。stage tracking 不包含 adapter 的 gyro 积分。另报 wall time，反映包括读盘解码的实际离线回放速度。
- 本机启用每 50 帧协方差特征值诊断（计时外）；板端关闭此额外诊断，但保留每帧 finite/对角检查和后端原有负对角退出检查。
- “完整跑完”不代表严格保持半正定：本机 FP32 协方差抽样仍出现约 1e-7 量级的负最小特征值；这些结果不能证明全 FP32 的数值稳定性。

## 本机精度

全部完成 1970 帧，1782 帧初始化后输出，GT 816 帧，五组均达到上述相对准入门槛。

| 方案 | packed ATE/m | unpacked ATE/m |
|---|---:|---:|
| baseline | 0.2294 | 0.2290 |
| filter | 0.2376 | 0.2366 |
| imu | 0.2253 | 0.2318 |
| adaptive | 0.2257 | 0.2355 |
| combo | 0.2135 | 0.2243 |
| filterimu（补充） | 0.2368 | 未测 |

浅层路径约 31% 的点需要完整金字塔重跑；packed 下两目主 LK 平均时间从 IMU 完整金字塔的 1.072ms 增至 1.194ms，因此不能仅凭层数减少推断收益。最终以 A53 完整回放为准。本机耗时未作为板端速度替代。

## 复现

```sh
scripts/vio-compare/probes/lk-strategy/build.sh host fp32
scripts/vio-compare/probes/lk-strategy/build.sh a53 fp32
```

生成目录为 `install/openvins-lk-strategy/{host,a53}-fp32`，使用干净的 OpenVINS git archive，加 offline、headless、16×15 patch，再生成 FP32 后端。

用 `../backend-f32/prepare_configs.py` 为五组复制相同 YAML，只改变计时文件输出路径。将编译的 `build/run_fisheye` 放在运行目录根目录。A53 可使用 `aarch64-none-linux-gnu-strip --strip-debug` 生成部署副本；完整符号二进制保存在本机 build 内。

```sh
OPENVINS_NUMERIC_EIGEN=1 OPENVINS_FP32_PACKED_MA=1 \
  scripts/vio-compare/probes/lk-strategy/run.sh \
  /absolute/run/root /absolute/uzh-input \
  baseline-r1 filter-r1 imu-r1 adaptive-r1 combo-r1

# 设备上所有回放共用一个锁，保证独占 CPU；不启用额外特征值诊断。
OPENVINS_FP32_PACKED_MA=0 flock /mnt/data/vio-compare/run.lock \
  sh /mnt/data/vio-compare/lk-strategy-fp32-unpacked/run.sh \
  /mnt/data/vio-compare/lk-strategy-fp32-unpacked \
  /mnt/data/vio-compare/backend-f32/uzh-input baseline-r1

python3 scripts/vio-compare/probes/lk-strategy/analyze.py \
  /absolute/run/root/results-uzh \
  --variants baseline-r1 filter-r1 imu-r1 adaptive-r1 combo-r1 \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv
```

分析每个运行的完整 GT 段，不会因为另一个方案提前失败而把全部成功方案的精度缩成失败方案的短前缀。失败方案只记录已落盘行的局部耗时，不可当作完整性能。

本机结果：`install/openvins-lk-strategy/host-fp32{,-unpacked}/results-uzh/summary.json`。板端 packed 部署：`/mnt/data/vio-compare/lk-strategy-fp32`；unpacked 部署：`/mnt/data/vio-compare/lk-strategy-fp32-unpacked`。二者均为同一个全 FP32 二进制。

## A53：全 FP32 packed，第一轮

CPU 时钟记录为 1 GHz，无 ROS，串行离线回放、不丢帧。五个成功方案都完成 1970 帧、1782 帧初始化后输出、816 个 GT 帧。

| 方案 | 相机全段 ms/帧 | 初始化后后端 ms/帧 | 两目主 LK ms | RANSAC ms | 板端 ATE/m |
|---|---:|---:|---:|---:|---:|
| baseline（全关） | **负协方差退出，无完整成绩** | — | — | — | — |
| filter | 67.460 | 22.533 | 13.021 | 7.483 | 0.2108 |
| imu | 70.657 | 22.156 | 12.946 | 10.413 | 0.2070 |
| filterimu | 66.873 | 21.673 | 13.030 | 7.097 | 0.2161 |
| adaptive | 79.334 | 22.284 | 14.589 | 17.177 | 0.2222 |
| combo | 71.987 | 21.044 | 14.394 | 11.506 | 0.2224 |

后端分项只统计初始化后的帧；LK/RANSAC 分项统计全段，两种口径不能直接相加。相机全段计时包含预测开销。

全关 baseline 在 packed 与 unpacked 两条全 FP32 路径中都触发 `StateHelper::EKFUpdate()` 负对角退出。日志已到 `frames=200 initialized=12`；轨迹 CSV 的尾部由于 std::exit 未析构 ofstream 而丢失，不能把已落盘行数当成实际失败帧。没有禁用检查、截断协方差或修改初始化来强行完成。

因此不把基线短前缀的 27ms 当完整速度，也不与此前 mixed/15×15 的数字直接计算收益。用同一 packed 后端、同一窗口、同一配置、完整成功的两组做单宏差分：

| 要评估的宏 | 对照 → 实验 | 第一轮耗时变化 |
|---|---|---:|
| 过滤失败点 | imu → filterimu（IMU 初值均开启） | 70.657 → 66.873ms，减少 5.36% |
| IMU 初值 | filter → filterimu（过滤均开启） | 67.460 → 66.873ms，减少 0.87%；两轮均值减少 0.75% |
| 浅层回退 | filterimu → combo（过滤与 IMU 均开启） | 66.873 → 71.987ms，增加 7.65% |

浅层回退还有一组独立对照 imu → adaptive，耗时增加 12.28%。约 31% 的点再次跑完整金字塔，约 4.4% 的相机整帧重跑。以 filterimu → combo 看，LK 增加约 1.36ms、RANSAC 增加约 4.41ms，后端略有减少；少跑金字塔层数被额外工作抵消。

IMU 初值在这个 30Hz FPV 数据集上没有降低主 LK 耗时；全流程的小幅变化同时包含 RANSAC 和后端观测集合的变化，不应解释成 LK 迭代显著减少。

实际包含读盘/PNG 解码的回放速度：filter 1970/230.596≈8.54fps，filterimu 1970/228.933≈8.61fps。只按相机算法计时推算分别为 14.82、14.95fps，不能当作这里的实际离线回放帧率。

结论：过滤 LK 失败点值得保留为候选；IMU 初值没有明显速度收益；这一版浅层回退为负收益，不启用。三个策略仍为独立实验宏，没有更改生产默认。全 FP32 的基线数值问题仍然存在，单个组合跑完不能证明其通用稳定性。

## 最终复测

使用相反的顺序重复 filter 与 filterimu：第一轮 filter 在前，第二轮 filterimu 在前。两组均完整跑完，且各自两轮的全部非计时轨迹/协方差字段逐行完全一致。

| 方案 | 第一轮全段 ms/帧 | 第二轮全段 ms/帧 | 两轮全段均值 | 两轮初始化后均值 | ATE/m |
|---|---:|---:|---:|---:|---:|
| filter | 67.460 | 67.162 | 67.311 | 71.525 | 0.2108 |
| filterimu | 66.873 | 66.733 | 66.803 | 70.932 | 0.2161 |

IMU 初值的全段收益为 0.754%，不足 1%；初始化后收益为 0.830%。即使两轮都略快，也不能算较大收益。最佳组合 filterimu 的初始化后算法处理率约 14.10fps（1000/70.932），包含图片读盘和解码的实际离线回放约 8.5fps。

三个实验宏继续保持 opt-in，不接入生产默认。浅层实现保留用于复现负收益结果。详细精度、分项、数值诊断、二进制 SHA256 和重复验证保存在本目录 [experiment-results.json](experiment-results.json)，原始日志/轨迹/分项数据在 `install/openvins-lk-strategy/`，完整符号二进制仍在各 build 目录。

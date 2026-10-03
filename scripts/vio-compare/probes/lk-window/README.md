# A53 LK 窗口比较

比较灰度 LK 的 `15×15`、`16×15`、`16×16` 窗口，保留同一份 OpenCV
3.4.20 静态库和同一套 UZH-FPV `indoor_forward_6` 配置。

## A53 实测结果

2026-10-02 至 2026-10-03 完成六次完整回放。每个窗口每轮都完成 1970 帧，初始化后
1782 帧，退出码均为 0。时钟记录和运行中抽查均为 1 GHz。
两轮采用相反的窗口顺序；同一窗口的位姿、初始化和数值诊断字段逐行一致。
两轮整段视觉处理均值的差异均小于 0.2%。下表完整回放耗时为两轮均值。

| 窗口 | 固定输入 LK / ms | 整段视觉 / ms | 初始化后视觉 / ms | 前端 tracking / ms | 后端 / ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 15×15 | 23.31 | 83.72 | 89.38 | 59.02 | 29.70 |
| 16×15 | 16.73 | 76.34 | 81.44 | 51.74 | 29.03 |
| 16×16 | 17.80 | 76.48 | 81.56 | 52.61 | 28.30 |

相对 15×15：16×15 固定输入 LK 时间降低 **28.25%**，完整视觉时间降低
**8.82%**；16×16 分别降低 **23.67%** 和 **8.64%**。
三个窗口的固定输入 LK 有效跟踪比例分别为 98.614%、98.606%、98.567%。

| 窗口 | 公共真值区间 ATE RMSE / m | 末帧位置相对 15×15 / m | 末帧姿态相对 15×15 / ° |
| --- | ---: | ---: | ---: |
| 15×15 | 0.21435 | 0 | 0 |
| 16×15 | 0.23294 | 0.18018 | 0.26763 |
| 16×16 | 0.24578 | 0.24873 | 0.65832 |

ATE 使用共同的 **816 个真值帧**，沿用既有基准的 -16.684572 ms 时间修正，
仅做 SE(3) 对齐，不拟合尺度。现有真值不覆盖整段录像；末帧相对基线的差异
不等于漂移误差。

结论：本次数据上，**16×15 是更好的速度与精度折中**。
16×16 的整体速度与其接近，ATE 更高。两种 16 列窗口的 ATE 均高于 15×15，
因此这不是保持轨迹不变的优化。窗口宽度变化会改变 patch 的像素集合和
亚像素采样位置，跟踪点、迭代和后端观测也随之改变。
若需要保持原有 15×15 的跟踪几何，后续应单独优化其 7 像素标量尾部。
这轮比较使用隔离实验，未缩小计算位宽。比较结束后，按用户要求正式构建
默认启用 16×15；`OPENVINS_LK_WINDOW_16X15=OFF` 可在重新构建时恢复 15×15。

完整 CSV、日志、时钟记录、`comparison.json` 和 `summary.json` 保存在
`install/openvins-lk-window/mixed/results-window/`；构建命令及二进制哈希
分别在该实验目录的 `commands.json`、`sha256.json`。

### 默认启用后的验证

正式的 `openvins-lk-window.patch` 将窗口定义放在头文件的默认初始化中，
CMake 选项 `OPENVINS_LK_WINDOW_16X15` 默认 ON，宏同时传给库和调用方。
OFF 配置已核实两者均使用宏值 0；ON 配置均使用宏值 1。

已交叉编译局部 FP32 版本，并更新设备
`/mnt/data/vio-compare/backend-f32/run_record.mixed` 和 `run_fisheye.mixed`。
旧程序保留为同目录下的 `.pre-lk16x15` 备份。
默认版本在设备回放前 300 帧成功，112 帧初始化后输出；除计时外，
300 行输出与此前的 16×15 实验逐行一致，与 15×15 的输出有 111 行不同。
验证日志、CSV 和二进制哈希保存在
`install/openvins-lk-window/default-enabled/`。

## 实验方式

- TideSense SG2002 A53 1 GHz，GCC 11.3，关闭 ROS，OpenCV 单线程。
- 完整轨迹测试采用局部 FP32 矩阵乘法，协方差和更新求解保留 FP64：
  `OPENVINS_EKF_MA_MODE=packed_float`。
- 每个窗口完整回放 1970 帧两次；第二轮反向排序。
- `compute_ms` 只计 `feed_measurement_camera()`，不含 PNG 解码、额外 IMU
  输入、CSV 输出和数值诊断。逐阶段时间单独写入 CSV。
- 轨迹以三个窗口共同的真值覆盖帧比较，使用 SE(3) 对齐，不拟合尺度。
  数据集现有真值只覆盖部分录像，末帧相对基线差异不是真值误差。
- 固定输入 LK 测试：在录像第 201 帧以后的区间均匀抽取 32 对连续图像，
  两颗相机各选最多 200 个 FAST 点。三个窗口使用完全相同的图像和点，
  预先构建金字塔，再只计 `calcOpticalFlowPyrLK()`。
  采用与 OpenVINS 相同的最大层数 5、最多 30 次迭代、epsilon 0.01、
  `OPTFLOW_USE_INITIAL_FLOW`，初值均为前帧坐标。
  每组预热一次、测三次，并轮换窗口顺序。
  有效点数为 LK 的 status，不包含 RANSAC 几何检查。
  它不是前端实际全部匹配调用的重放，输入点数和初值也可能不同，因此绝对
  毫秒数不能当作整帧 LK 开销；实际流水线收益以完整回放计时为准。

## 隔离方式

`build.py` 复制 `TrackKLT.cpp/.h`，只替换静态库里的 `TrackKLT.cpp.o`。
窗口赋值位于 `feed_new_camera()` 的入口、金字塔构建之前；构造函数内联在
其他编译单元，因此不能只修改头文件里的默认值。

实验代码受 `OPENVINS_LK_WINDOW_EXPERIMENT` 控制，尺寸由
`OPENVINS_LK_WINDOW_WIDTH/HEIGHT` 指定。未启用宏时保持原逻辑。
首帧日志打印实际窗口尺寸。原始源码、库、设备上的原运行程序均不覆盖。
15×15 基线的非计时输出已与之前的局部 FP32 回放逐行核对一致。
实验入口会在首帧预处理前覆盖正式默认窗口，因此仍能生成三个比较版本。

## 复现

需要先有 `backend-f32/build_variant.sh a53 mixed` 的隔离构建产物：

```bash
python3 scripts/vio-compare/probes/lk-window/build.py --precision mixed
python3 scripts/vio-compare/probes/lk-window/build_pairs.py
python3 scripts/vio-compare/probes/backend-f32/prepare_configs.py \
  install/openvins-backend-f32/perf-uzh/config-uzh-perf \
  install/openvins-lk-window/mixed --name window \
  --runtime-root /mnt/data/vio-compare/lk-window-mixed \
  --variants 15x15-r1 16x15-r1 16x16-r1 16x16-r2 16x15-r2 15x15-r2
```

把三个 `run_fisheye.*`、六个配置目录及 `run_a53.sh` 部署到设备的
`/mnt/data/vio-compare/lk-window-mixed/`。设备数据复用
`/mnt/data/vio-compare/backend-f32/uzh-input/`。

```sh
sh run_a53.sh /mnt/data/vio-compare/lk-window-mixed
flock /mnt/data/vio-compare/run.lock ./lk_pairs \
  /mnt/data/vio-compare/backend-f32/uzh-input > results-window/lk-pairs.csv
```

第二条命令需要先部署 `lk_pairs`。取回 `results-window` 后运行：

```bash
python3 scripts/vio-compare/probes/lk-window/summarize.py \
  install/openvins-lk-window/mixed/results-window \
  --gt install/schurvins-gcc11-static/public-datasets/uzh-fpv/indoor_forward_6_replay/groundtruth.csv
```

输出 `comparison.json` 和 `summary.json`；汇总脚本也核实同一窗口两轮的
非计时轨迹和数值诊断字段完全一致。

## 初始完整 FP32 实验

最先沿用 perf 的完整 FP32 加打包后端测试：15×15 完成 1970 帧，平均
77.04 ms；非计时输出与先前的 15×15 结果逐行一致。
16×15 在日志已报告完成 200 帧后触发 `StateHelper::EKFUpdate()` 的
负协方差对角检查，退出码 1。输出有缓冲，不能用 CSV 最后一行推断准确退出帧。

因此完整窗口比较改用协方差仍为 FP64 的局部 FP32 版本。
这次失败说明此前完整 FP32 的成功结果不能推广到不同跟踪配置，不能仅凭
固定输入 LK 的加速就把 16×15 配合完整 FP32 后端直接启用。
原始失败日志保存在 `install/openvins-lk-window/results-window/`。

固定输入测试与后端无关，三窗口各 192 次计时调用；原始数据保存在上述
目录的 `lk-pairs.csv`。

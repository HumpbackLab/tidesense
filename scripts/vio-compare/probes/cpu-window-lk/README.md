# 16／32 点 CPU 窗口 LK 微测试

在 TideSense A53 1 GHz 上测一小批完整时序 LK 的成本，用来评估前端尾部任务
能否由 A53 在后端结束后领取。没有接入任务分流，也没有测试逐相机提前发布几何。
现有 OpenVINS、C906 固件及启动配置均未修改。

## 实现与计时范围

复用已有五级 U8 金字塔，每点、每层只生成模板窗口所需的 **17×16 Scharr**，
再调用 SDK 原 OpenCV 3.4.20 的 NEON LK 主循环，窗口 16×15、最多 30 次迭代、
epsilon 0.01。灰度边界为 REFLECT_101，图像外的梯度为零。A53 的窗口 Scharr
使用 int16 NEON，结果与 OpenCV 整图 Scharr 逐字节一致。

使用 OpenCV 支持的“灰度／已提供梯度”金字塔接口，避免其内部重新生成全图
梯度。梯度 Mat 保留整层的索引布局，但只写入被选择点的模板邻域；该持久工作区
在循环外分配，640×480 五级约 1.88 MiB。窗口重叠时重复计算，不做跨点缓存。
没有计算前端未使用的光度误差数组，保留原位置、有效性、特征值及收敛判断。

测量包括：窗口梯度、完整五层 LK、输出坐标／状态及 OpenCV 包装层成本。
`window_ion_sync` 还包含两组共享灰度金字塔的 A53 cache invalidate。
图像读取、均衡、金字塔构建、一次性工作区分配和 TPU 命令生成在计时外。
它假设调度点已有金字塔，并不代表从原图开始的前端总成本。

TPU 对照使用现有 16 通道、窗口缓存和双批流水固件。`device_ms` 是 C906 内部
完整 LK 时间；TPU `wall_ms` 还包括控制缓存同步、邮箱及返回。CPU／TPU 顺序
测量，尚未包括并行时的内存争用、任务认领及合并协议成本。

## 设备结果

UZH-FPV `indoor_forward_6`：200、450、700、1000、1020、1050、1450、1940 帧，
两颗相机，连续帧和隔一帧各测一次。每幅图 FAST50 均匀抽取 75 点并做亚像素
精化，再分别取头部／尾部 16 或 32 点。这里的“尾部”是测试点列表尾部，
不是从实际 VIO 运行中导出的历史跟踪队列。

共 128 个案例；每个模式先预热一次，再测 15 次，循环旋转模式顺序。
四种模式共 7680 次计时。以下按 **尾部点、含共享缓存刷新** 汇总：

| 项目 | 16 点 | 32 点 |
| --- | ---: | ---: |
| 完整 CPU LK 平均 | 2.230 ms | 4.040 ms |
| 中位数 | 1.803 ms | 3.432 ms |
| P95 | 4.097 ms | 7.081 ms |
| 本批样本最大 | 4.207 ms | 7.298 ms |
| 窗口梯度平均 | 0.352 ms | 0.662 ms |
| LK 主循环等平均 | 1.782 ms | 3.281 ms |
| 共享灰度缓存刷新平均 | 0.095 ms | 0.097 ms |

连续帧的尾部 16 点平均 1.910 ms、P95 2.731 ms；隔一帧时平均 2.550 ms、
P95 4.186 ms。32 点对应平均 3.478／4.601 ms，P95 4.514／7.168 ms。
这是同一数据集不同图像间隔，不能解释成算法实际运行帧率。

头部和尾部一起汇总时：

| 路径 | 16 点平均 | 32 点平均 |
| --- | ---: | ---: |
| 原 OpenCV、全图梯度已预计算，仅 LK | 1.771 ms | 3.209 ms |
| 窗口梯度＋LK，heap 金字塔 | 2.005 ms | 3.759 ms |
| 窗口梯度＋LK，共享金字塔含刷新 | 2.105 ms | 3.862 ms |
| C906＋TPU，内部 LK | 1.584 ms | 2.242 ms |
| C906＋TPU，含邮箱和缓存同步 | 1.972 ms | 2.635 ms |

仅从耗时看，后续宜先尝试 **每帧最多 16 点**，领取时留约 5 ms 预算。
32 点在困难样本接近 7.3 ms，容易占满等待窗口。上述最大值不是硬实时上界，
16 点也不能保证塞进任意一个只有 2–3 ms 的空闲窗口。

实际加速量取决于领取时机、C906 剩余任务以及双批配对；CPU 花 2.2 ms 并不
等于整帧节省 2.2 ms。暂不据此宣称达到 20 Hz。

## 数值检查与限制

* 主机、A53 自检：6 种尺寸／纹理组合、438 个点，包括常量图像、负坐标和
  图像边缘。未填区域使用污染值；需要读取的梯度与原实现逐字节一致，坐标及
  有效标志与原 OpenCV 完全一致。主机 ASan／UBSan 检查通过。
* FPV 的 3072 个批次点实例中，窗口 CPU 与完整梯度 CPU 的坐标和有效标志
  完全一致；所有重复调用也检查一致性。不同批次有点重叠，3072 不是独立点数。
* CPU／TPU 的 LK 有效标志 3072／3072 一致，但坐标不是逐位等价。共同有效点
  的坐标差 P95：16 点组约 0.029 px，32 点组约 0.017 px。
* 困难的隔帧样本有 7 个不同的输入点坐标差超过 1 px。最大约 103 px，位于
  第 1050 帧左图，CPU 输出已在图像外；两条结果均在画面内的另一个点也相差
  约 27.6 px。因此不能把“大多数点误差小”当作混合分流的 VIO 精度保证。

这些差异来自原 CPU／OpenCV 与现有 TPU 路径的不同数值及收敛结果，新增窗口
Scharr 与完整 CPU 参考一致。后续分流必须保留边界／几何剔除，再跑完整 FPV
轨迹验证；本次没有实现任务队列或修改前端数值规则。

## 复现

```sh
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch host
install/openvins-c906-frontend/cpu-window-lk/window_lk-host --selftest
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch host --sanitize
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  install/openvins-c906-frontend/cpu-window-lk/window_lk-host-asan --selftest
python3 scripts/vio-compare/probes/cpu-window-lk/build.py --arch a53
```

设备实验目录 `/mnt/data/openvins-c906-front/cpu-window-lk`。部署
`window_lk-a53` 后使用已验证的生产实验固件，独占 TPU 运行：

```sh
exp=/mnt/data/openvins-c906-front/cpu-window-lk
mkdir -p "$exp/results"
C906_TOOLS_DIR=/mnt/data/openvins-c906-front/tools OPENVINS_TPU_FRONTEND=1 \
  /mnt/data/openvins-c906-front/live/with-tpu.sh "$exp/window_lk-a53" \
  /mnt/data/openvins-c906-tpu/dataset "$exp/results" \
  200,450,700,1000,1020,1050,1450,1940 15 --tpu
```

退出后恢复普通小核固件和 Linux TPU 驱动。省略 `--tpu` 可只测 CPU（A53
版本仍提供 ION 共享灰度对照），不需要接管 TPU。先创建输出目录。

抓取 `results/` 后汇总：

```sh
python3 scripts/vio-compare/probes/cpu-window-lk/analyze.py \
  install/openvins-c906-frontend/cpu-window-lk/results
```

`timings.csv` 保存每次调用，`points.csv` 保存 CPU／TPU 点级结果；
`summary.json` 同时包含头尾分组、图像间隔、单次 P95／最大值，以及最慢案例
的重复中位数。大差异点另外保存在 `outliers.json`。

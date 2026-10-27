# RV1103 OpenVINS 单目回放耗时与 perf（2026-10-25）

**已在 perf 诊断镜像上完成两轮无 perf 的 400 帧计时和一轮硬件 cycles 采样。**
纯 VIO 平均 148.19 ms/帧；加图像文件读取/解码为 170.20 ms/帧，折算离线
吞吐约 5.88 fps。主要成本是 MSCKF 后端和 LK，测量压缩的长尾尤其明显。

## 条件与有效性

- 设备：`172.32.0.93`，RV1103、单核 Cortex-A7，固定 performance/1.104 GHz。
- 内核：Linux 5.10.160，PERF_EVENTS、ARM_PMU，驱动报告 5 个硬件计数器。
- 系统：64 MiB 物理 RAM、24 MiB 摄像头 CMA，Linux MemTotal 32824 KiB，无 swap；
  诊断镜像关闭摄像头/媒体/Wi-Fi 模块自动加载，通过 RNDIS/SSH 访问。
- 算法：OpenVINS FP64、OpenCV/OMP 单线程，200 目标特征、11 clones、max_slam=0；
  FEJ、相机在线内外参及时间偏移标定开启，保留 χ² 外点门控。
- 输入：EuRoC V1_01_easy 的 25 秒片段，单目 752×480；PNG 无损转为 PGM。
  没有在此轮使用 OV9281/板载 IMU 实时输入。
- 原生初始化，每轮总处理 495 帧，5 帧预热后统计 400 帧，输出 405 个初始化后状态。
  两轮基线汇总为 800 个测量帧；下表 P95 从合并样本计算，不是平均两个 P95。
- 基线均值分别 148.4029、147.9816 ms，相差约 0.28%。
- 采样轮均值 149.5356 ms，比两轮基线均值高约 0.91%；耗时结论采用无 perf 的数据。
- 三轮及旧内核基线的轨迹文件逐字节一致，SHA256 为
  `ac877f31b8ebb2f7c502d3d1a13e024fdbb1056e99dc8f5ab1fcbfd4ecb72478`。
- 轨迹 SE(3) 刚体对齐、不拟合尺度，覆盖 20.2 s：位置 ATE RMSE **0.046281 m**，
  最大误差 0.096252 m。χ² 检验 4221 次，拒绝 50 次；最终状态有限、协方差对角元为正。
- 两轮无 perf 的峰值 RSS 分别 13784、13556 KiB，约 13.46、13.24 MiB。

这是该短序列和上述配置的离线计算成本，不等于含摄像头采集、视频驱动和发布服务
的产品实时帧率；也不能把关闭驱动后的结果当作 perf 内核带来的优化收益。

## 总体耗时

单位 ms/帧。前端/后端和其子项存在包含关系，不能重复相加；各项 P95 也不能相加。

| 阶段 | 平均 | P95 |
| --- | ---: | ---: |
| 前端跟踪总计 | 66.488 | 82.532 |
| IMU 传播及状态扩增 | 3.001 | 3.210 |
| MSCKF 更新 | 76.631 | 232.545 |
| 边缘化及清理 | 0.997 | 1.100 |
| 后端总计 | 80.629 | 236.543 |
| 适配层、IMU 喂入等其他开销 | 1.062 | 1.471 |
| **纯 VIO** | **148.192** | **309.062** |
| 图像文件读取及 PGM 解码（VIO 之外） | 22.005 | 27.469 |
| **VIO + 读取解码** | **170.197** | **330.537** |

纯 VIO 的平均耗时折算约 6.75 fps；计入读取解码约 5.88 fps。
没有以等待图像的实时节拍运行，因而这些是计算吞吐估算。

## 前端细分

| 阶段 | 平均 | P95 |
| --- | ---: | ---: |
| 直方图均衡 | 4.537 | 4.612 |
| 金字塔及导数构建 | 15.583 | 17.504 |
| 特征补点：网格、FAST、亚像素及筛选 | 4.321 | 10.948 |
| LK 光流 | 33.676 | 40.384 |
| 匹配点去畸变/归一化 | 3.342 | 3.922 |
| RANSAC 基础矩阵 | 0.864 | 1.466 |
| 跟踪筛选、数据库更新及其他 | 3.995 | 4.674 |

细分计时使用 steady_clock；OpenVINS 总体阶段计时来自其现有统计文件，
包含边界处的管理开销，因此细分之和与前端总计有少量差异。

## MSCKF 细分

| 阶段 | 平均 | P95 |
| --- | ---: | ---: |
| 清理过期观测 | 0.081 | 0.144 |
| 构建相机克隆位姿、三角化与优化 | 2.970 | 6.654 |
| 构建线性系统：Jacobian、零空间投影、χ² 检验及拼接 | 23.336 | 71.436 |
| 测量压缩 | 27.848 | 125.646 |
| EKF 状态与协方差更新 | 22.053 | 27.930 |

测量压缩平均 27.85 ms，但 P95 达 125.65 ms，是后端长尾的主要来源之一。
本轮没有记录压缩矩阵尺寸，尚未据此定位具体是哪一类特征批次造成大更新。

## 硬件 perf 热点

板端使用 `perf record --clockid mono -e cycles:u -F 99 -m 64 -B`，
在电脑用匹配的未 strip ELF 解析符号。采集到 6436 个样本，按每帧 monotonic
起止区间筛选，只保留初始化和预热之后 **VIO 函数处理区间内的 5895 个样本**。
读取/解码、初始化、轨迹导出和帧间记录不纳入下面的分母。
PERF_RECORD_LOST、THROTTLE、UNTHROTTLE 均为 0。
内核日志记录了 perf 中断成本触发的全局采样上限自动下调；结束后上限为
7500 Hz，仍高于本轮请求的 99 Hz，没有把该事件压到更低的请求频率。

占比按采样 period 加权，表示函数自身的用户态 cycles 占比，不是调用栈累计占比，
也不是直接测得的墙钟时间占比。

| 函数/计算 | cycles 占比 |
| --- | ---: |
| `cv::detail::LKTrackerInvoker::operator()` | 23.40% |
| Eigen FP64 `gebp_kernel<double, double, ...>` 矩阵乘法内核 | 19.17% |
| `UpdaterHelper::measurement_compress_inplace` | 17.63% |
| `cv::detail::ScharrDerivInvoker::operator()` | 4.17% |
| `StateHelper::get_marginal_covariance` | 2.35% |
| `cv::PyrDownVecH` | 2.14% |
| `__memcpy_neon` | 1.90% |
| Eigen FP64 左矩阵打包 `gemm_pack_lhs` | 1.88% |
| 直方图 LUT 应用 | 1.65% |
| `Eigen::JacobiRotation<double>::makeGivens` | 1.56% |

优先优化方向是测量压缩及后端 FP64 矩阵运算，其次是 LK 和金字塔。
GEMM 会服务多个后端调用，当前平面采样不足以把 19.17% 全部归入某个 EKF 子步骤。
此轮只测量，没有修改精度、特征数、滑窗大小、初始化或外点检验阈值。

## 复现与产物

板端目录 `/root/openvins-profile-20261025/`：

```sh
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
sh /root/openvins-profile-20261025/run-board.sh baseline baseline-perfkernel-1
sh /root/openvins-profile-20261025/run-board.sh perf cycles-perfkernel
sh /root/openvins-profile-20261025/run-board.sh baseline baseline-perfkernel-2
```

采样、CSV、轨迹和运行日志都在板端 `results/`。测试结束后频率策略已恢复为
原来的 `ondemand`，回放与 perf 进程均已退出。

本机原始产物：`install/rv1103-openvins-20261025/results-perfkernel/`。
精确汇总也保存为 [results-20261025.json](results-20261025.json)。

```sh
/usr/lib/linux-tools-5.15.0-198/perf script --ns \
  -F time,period,event,ip,sym,dso \
  -i install/rv1103-openvins-20261025/results-perfkernel/cycles-perfkernel.data \
  --symfs install/rv1103-openvins-20261025/symfs > samples.txt
python3 scripts/vio-compare/probes/rv1103-profile/analyze.py \
  install/rv1103-openvins-20261025/results-perfkernel/cycles-perfkernel-detail.csv \
  --stages install/rv1103-openvins-20261025/results-perfkernel/cycles-perfkernel-stages.csv \
  --perf-script samples.txt
```

OpenVINS 阶段文件使用在线估计后的 IMU 时钟，detail CSV 使用原始相机时钟。
分析器按执行顺序、初始化帧数和小于半帧的时钟差检查配对；perf 窗口则完全使用
detail CSV 的 monotonic 纳秒时间，避免混用数据集时钟和设备运行时钟。

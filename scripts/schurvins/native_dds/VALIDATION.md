# VIO ROS 后端验证

## VIO 编译后端接入验证

2026-10-26：`SCHUR_ROS_BACKEND=micro-ros|native-dds|none` 三种完整 VIO 构建和安装包均生成并在 SG2002 验证。
VIO 使用公共 RosBridge 接口；micro-ROS 与 none 保持全静态 ELF，native-dds 使用静态估计器和私有 ROS 动态运行库。
原生包自动沿 ELF DT_NEEDED 收集依赖（含完整 VIO 额外需要的 libatomic），由包内 run.sh 使用匹配加载器启动。
安装清单覆盖原生库文件和可选 discovery XML。SDK build_tidesense_apps / build_all 将后端选择传给镜像安装器。

板端测试未使用相机和 IMU，未中断网页预览或覆盖设备现有应用。测试位于独立临时目录并在结束后清理。

| 验证 | 结果 |
| --- | --- |
| 三种包 SHA256SUMS 与 --help 后端标识 | 通过 |
| 三种包关闭通信后运行合成 VIO | frames=6 tracking=6 imu_fused=6 |
| micro-ROS 旧开启变量及通用 SCHUR_ROS | 通过；显式通用关闭优先 |
| 原生 DDS / micro-ROS 关闭通信的资源 | 1 个线程，0 个 socket |
| 原生 DDS 错误 discovery XML | 后台报告并重试，VIO 合成自检仍完成 |
| 错误开启变量、错误 native domain、none 版本强制开启 | 正确拒绝 |
| 通用生产桥接的 200 Hz 目标夹具 | 两种后端发布 PoseStamped 与 tracking true/false，数值、时间戳、坐标系和节点检查通过 |
| SDK 镜像安装与构建顺序回归 | 13 项通过，含 native 包选择、缺库及库内容损坏拒绝 |

发布夹具共提交 1400 个状态：1120 个有效位姿和 280 个跟踪无效状态。
原生 DDS 收到 1114 个位姿、1115 个 true 和 274 个 false；micro-ROS 收到 1080 个位姿、1080 个 true 和 264 个 false。
夹具从第一帧就发布，未等待连接和 DDS 发现；收到的消息值、时间戳顺序均正确。
原生日志 poses=1120、overwritten=6，micro-ROS 日志 poses=1080、overwritten_or_busy=56。
这些计数包括跟踪失效时主动清队以及连接建立期间的积压，未单独区分容量覆盖，不能据此断言零排队丢弃。
初始建立和跟踪失效清队会造成缺失，本项不表述为端到端无丢包或延迟保证。
本轮不重复 CPU 对比，先前独立通信测量保留在下方。

未验证真实硬件 VIO 轨迹精度、默认组播网络或多订阅者压力；这里验证后端选择、包装部署和通信行为。

| 后端 | 安装目录 | VIO SHA-256 |
| --- | --- | --- |
| micro-ros | schurvins-gcc11-static-micro-ros | `929914fd8c2a5feece9419c6db226f59eef5a4f0e2c4974ef95f4ce57205f42e` |
| native-dds | schurvins-gcc11-native-dds | `05954f40de4ff07e75a0e3c4adbe5ec9343b9f38d6040d4c72dfa1fa42def1ae` |
| none | schurvins-gcc11-static | `1f8e6c6e319678264874b93e248357d883a828d7600e268328bdfedaccc37091` |

证据：install/native-dds/vio-native-release.log、vio-micro-build.log、vio-none-build.log、
image-install-tests.log、vio-integration-check/results.json 及同目录每项板端 / 接收器日志。
Shell 语法、Python 编译检查和两个仓库的 git diff --check 通过。

以下为接入前独立通信程序的历史测量，测试代码和完整 VIO 程序不同。

---

# 2026-10-26 SG2002 原生 DDS 开销

板端 192.168.123.99，单核 A53 1 GHz。每个采样点均发布 PoseStamped 和 tracking=true 两条消息。
电脑使用普通 ROS 2 Humble / Cyclone DDS 订阅；micro-ROS 组经电脑 Agent 转发，原生组直接通过网络发布。
DDS 固定 peer、禁用组播，best effort，一个订阅节点；不包含图像、原始 IMU 或可靠传输压力。

## 发布进程 CPU

单核 100% 为满载，统计进程所有线程的 CPU 时间，包括测试驱动、队列和序列化。
每轮预热 5 秒后测量 30 秒；200 Hz 有订阅者各两轮，按 micro/native/native/micro 顺序交替。
电脑 Agent 和订阅者的 CPU 不计入表格；板端中断上下文的 CPU 未单独测量。

| 发布场景 | micro-ROS | 原生 DDS | 原生 DDS 差值 |
| --- | ---: | ---: | ---: |
| 20 Hz，一个订阅节点 | 0.65% | 1.07% | +0.42 个百分点 |
| 200 Hz，一个订阅节点 | 5.78% | 9.25% | +3.47 个百分点 |
| 200 Hz，无下游订阅者 | 5.73% | 3.65% | -2.08 个百分点 |

200 Hz 两轮 micro-ROS 为 5.7184% / 5.8464%，原生 DDS 为 9.2412% / 9.2634%。
均值差 3.4699 个百分点，相当于每秒多用约 34.70 ms 的 A53 CPU。
关闭通信时，同驱动 200 Hz / 20 Hz 的基线为 0.7350% / 0.0857%。
扣除各自基线后，200 Hz 通信增量约为 micro-ROS 5.05%、原生 DDS 8.52%。

无下游订阅者时，micro-ROS 的 Agent 仍在线，客户端继续发送给 Agent；原生 DDS 的数据发布路径因没有匹配订阅者而减少工作。
该项不等于停止调用 submit，也不等于 micro-ROS 没有 Agent 时的重连状态。

## 正确性与资源

6 轮有订阅者测量共收到 25200 个测量窗口内位姿，全部消息内容、frame_id、时间戳序列正确，零缺失、零重复。
每轮 200 Hz 窗口均收到 6000 / 6000 个位姿，20 Hz 为 600 / 600；收到的 tracking 均为 true。
两组日志均无队列覆盖或发布失败。接收频率约 200 / 20 Hz，接收间隔不是端到端延迟。

micro-ROS 测试进程 2 个线程，原生 DDS 9 个线程。getrusage 峰值 RSS 约为 1784 KiB 与 4900–4924 KiB。
这是独立测试进程峰值，不是接入 VIO 后的净内存增量；静态程序的 /proc VmRSS 在此内核上偏低，不用于结论。
两组每轮启动及前 5 秒预热的 CPU 时间保存在 warmup_cpu_s；它包含预热发布，不能当作纯节点初始化耗时。

本实验隔离了通信工作，没有运行真实 VIO；网络中断、多个订阅者、图像大包、默认组播发现与真实 VIO 调度竞争尚未验证。
原生 DDS 仍为独立实验程序，生产 VIO 沿用现有输出。实验结束后自身 Agent / 订阅者及板端测试目录已清理，网页预览保持运行。

## 复现与证据

复测见 [README](README.md)。原始数据：

- install/native-dds/results-20261026-164101/results.json
- 同目录每轮 *-board.log、*-received.json、*-receiver.log 和 board.txt、host.xml、board.xml
- install/native-dds/sources.json、build.log、bench-build.log

Native benchmark SHA-256：
`39f98e9e56ec1ccc9710f3e3bc08f1c5a311547c1069db407ac725e352c209a1`。

micro-ROS benchmark SHA-256：
`0742f2d86bceb32b4d14a76744450e724026de6489c67414c51571e87f339be0`。

## README.md 实验与验证记录

### 通信开销实验

对照组使用生产 `micro_ros_bridge.cpp` 及已有 `libmicroros.a`，另有通信关闭的基线。
以下是接入构建选项之前独立测试程序的测量方法。

### 比较方法

- SG2002 单核 A53，时钟由每次结果的 `board.txt` 记录。
- 同一生产线程按绝对期限提交 PoseStamped 与 tracking=true，两组通信线程均 nice=5。
- 两组均 best effort、64 条位姿 FIFO，队列满丢最旧；相同消息、时间戳、frame_id 与数值。
- 20 Hz 和 200 Hz；每次预热 5 秒后测量 30 秒，200 Hz 有订阅者的两组交换顺序各测两次。
- 测量整个发布进程 `CLOCK_PROCESS_CPUTIME_ID / CLOCK_MONOTONIC`，单核 100% 为满载。
  包含测试驱动、队列、序列化、发布与库内线程，排除电脑端 Agent / 订阅者 CPU，未单独计入板端中断上下文的网络开销。
- 另测 200 Hz 无下游订阅者：micro-ROS 的 Agent 保持在线；原生 DDS 没有匹配的数据订阅者。
- `receiver.py` 通过普通 ROS 2 订阅，核对测量窗口内的全部 600 / 6000 个位姿、消息内容、重复及接收间隔。
  tracking 消息不含时间戳，仅校验收到的值；不把接收间隔当作端到端延迟。
- RSS 取 `getrusage` 的进程峰值；`/proc/self/status` 同时保留线程数与内存字段。
  当前板端 `/proc` 的 VmRSS 对微型静态测试程序显示过低，结果不能将该字段当作准确内存用量。
- 不运行真实 VIO 或图像输出，不改动相机、IMU 或网页服务。结果衡量通信开销，不能直接等同于 VIO 集成后的总 CPU。

实测结果见 [验证记录](VALIDATION.md)。

### 复测

独立对照实验仍需主机 ROS 2、micro-ROS 构建缓存及主机 Cyclone DDS：

```bash
scripts/schurvins/native_dds/build.sh
scripts/schurvins/native_dds/build-bench.sh
source scripts/schurvins/native_dds/host-env.sh
read -rs -p 'Board password: ' TIDE_DEVICE_PASSWORD
export TIDE_DEVICE_PASSWORD
python3 scripts/schurvins/native_dds/run-board.py --seconds 30
unset TIDE_DEVICE_PASSWORD
```

`run-board.py` 需要 Paramiko，可使用 `tide_configurator/.venv/bin/python`。
当前测试网卡地址固定为电脑 `192.168.123.2`、板端 `192.168.123.99`，使用独立 domain 37、Agent UDP 18889。
DDS 使用固定 peer、禁用组播。测试程序不使用相机，可与现有网页预览并行。
运行器只关闭自身启动的 Agent / 订阅者，结束后删除自身上传的 `/mnt/data/tide-dds-bench-*` 目录。
不要在运行期间同时启动第二份同 domain 的测试。

构建源和库分别保存在 `install/native-dds/ws`、`install/native-dds/package`。
原生方案为动态库，micro-ROS 对照为现有静态库；均使用 SDK GCC 11.3 / Cortex-A53 Release 编译。
原生运行时使用同工具链配套的动态加载器和 libc，放在私有测试目录，不替换设备系统库。
具体源码提交记录在 `install/native-dds/sources.json`；底层 DDS 安全功能与共享内存关闭。
原始日志、库包、每轮 CPU 和接收统计保存在 `install/native-dds/results-*`。

架构参考：[rclc](https://github.com/ros2/rclc/tree/humble)、[ROS 2 Cyclone DDS RMW](https://github.com/ros2/rmw_cyclonedds/tree/1.3.5)。

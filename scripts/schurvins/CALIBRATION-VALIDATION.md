# 双目 / IMU micro-ROS 标定模式验证

## 2026-09-20 联合采集验证

SC7U22 IIO / INT1，硬件 200 Hz；同一启动时钟偏移映射图像 VI PTS 和 IMU IRQ 时间。

| 图像请求帧率 | 收到图像对 | 图像实际帧率 | 收到 IMU | IMU 采样率 | 最大 IMU 时间间隔 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 fps，选择 200 对 | 197 | 16.897 Hz | 2336 | 199.536 Hz | 5.302 ms |
| 5 fps，选择 50 对 | 49 | 5.000 Hz | 1977 | 199.535 Hz | 5.288 ms |

两轮收到的全部 IMU 时间戳与六轴 SI 数值逐条匹配板端 CSV，图像与 IMU 的时钟偏移相同。
无 IMU 丢弃或迟到间隔；发送 FIFO 高水位均为 11。图像单邮箱允许覆盖，
20 fps 运行发生 2 次覆盖/繁忙，实际接收吞吐不保证等于请求帧率。
仅图像回归：请求 5 fps，选择 25 对，收到 24 对、5.000 Hz。
Agent 缺席时 FIFO 到 2000 条后返回错误；已建立连接中断时返回错误；SIGINT 正常退出。
三个场景都停止 FSIN，IIO `buffer/enable` 恢复 0。GCC 11 micro-ROS ON/OFF 均编译为全静态 ELF，
OFF 版本明确拒绝标定模式；独立算法自测通过。
原始记录：`install/calibration-imu/{real-20,real-5,images-only}/` 与 `failures.log`。

本轮验证联合采集、单位与时间戳传输，尚未使用多轴运动数据求解 Kalibr 外参。
下面保留早期仅图像模式的验证记录。

## 2026-09-18 仅图像验证

日期：2026-09-18。板端：TideSense / SG2002 A53，192.168.123.99。
GCC 11 全静态应用，micro-ROS Humble；相机左 0x10 / 2 lane、右 0x60 / 1 lane，
FSIN 20 Hz，640×400 mono8。没有运行 VIO 或 IMU 采集。

## 最终图像传输配置与结果

板端 XRCE UDP MTU 1400，输出历史 32、输入历史 4，可靠连续分片。
电脑 Agent 与 ROS 订阅端通过本机 loopback 单播通信，DDS UDP 最大消息 1400，
Agent socket 缓冲 4 MiB；Cyclone 接收缓冲至少 2 MiB。

| 测试 | 收到的完整图像对 | 按采集时间计算的接收帧率 | 校验 |
| --- | ---: | ---: | --- |
| 模拟相机，指定 7.5 fps，选择 80 对 | 79 | 7.494 Hz | 全部像素、左右对应关系、元数据、递增时间戳通过 |
| 真实相机，指定 5 fps，选择 50 对 | 49 | 5.000 Hz | 两路尺寸、长度、编码、frame_id、配对与时间戳通过 |
| 真实相机，指定 20 fps，选择 200 对 | 199 | 20.000 Hz | 同上；采集 200 对，无待发图像覆盖 |
| 真实相机，指定 10 fps，选择 220 对，中途重启 Agent | 186 | 重启前后均约 10.000 Hz | 自动恢复，重启后约 0.34 秒收到第一对 |

10 fps 重连测试重启前收到 53 对、重启后收到 133 对；中断期间不补发历史数据。
包含停机间隔的整体平均值为 8.486 Hz，不应误认为正常连接时只能达到该帧率。
所有上述测试的接收端错误列表均为空。
数量差异包含发现过程、停机和退出；不同 ROS 话题不是原子交付。
这些短时实测值不构成任意负载下的帧率或最坏恢复时间保证。

真实相机日志均确认启动 FSIN、两颗 OV9281 初始化成功，结束后停止 FSIN 并释放 VI/ISP。
VI 到达时间有毫秒级左右差异，不是测量曝光偏差；两张 ROS 图像使用公共配对时间戳。
板端系统时钟仍为 1970 年，测试没有修改设备时钟；正式采集前应正确设置时钟。
本次验证图像传输，不代表 AprilGrid 标定或 VIO 精度已经验证。

## 开关、错误与回归

- micro-ROS OFF、ON 两个版本均编译成功；目标 ELF 全静态。
- 未编译 micro-ROS 或运行时关闭时，请求标定在启动硬件前明确失败。
- 缺少 `--fps`、NaN、超过 20 fps 等输入被拒绝。
- 相机未设置 FSIN 时，标定模式拒绝继续。
- Agent 缺席时，有限帧运行能正常结束；持续运行可用 SIGINT 正常退出。
- 原位姿/跟踪发布夹具重新验证通过：245 条正确位姿，跟踪丢失状态、延迟启动 Agent、
  Agent 重启和节点发现均通过。
- 独立 VIO 的关闭/默认关闭/显式关闭/Agent 缺席 CSV 一致性结果见原始回归日志。

## 诊断过程与主机环境限制

本机是 WSL2，内核 `6.18.33.1-microsoft-standard-WSL2`。测试开始时，普通 ROS
接收端及 `ss` 阻塞在 `rtnl_dumpit` / `rtnetlink_rcv_msg`，读取 `/proc/net/wireless`
也会等待 RTNL。这个环境问题发生在收到图像之前，单独调整 DDS XML 无法消除。

为了完成传输验证，仅对主机测试进程使用临时 `LD_PRELOAD`：返回从 `/proc/net/fib_trie`
与 `/sys/class/net` 已确认的 lo/eth3 接口信息，并将无线接口枚举视为空（测试使用
有线 RNDIS 和 loopback）。数据收发、消息序列化和图像像素没有被替换；板端没有加载此库。
该测试辅助代码保存在忽略目录 `install/micro-ros/host-net-workaround/`，不进入产品包。
**正式使用本机 ROS 工具前，应先恢复 WSL 的正常网络查询能力。**

第二个问题是主机 DDS 的大 UDP 包出现 IP 分片重组失败：Agent 调试日志确认完整
256068 字节的 Image CDR 已写入 DDS，但订阅端只收到零散图像。将 DDS UDP 最大
消息改为 1400、增大缓冲后，上表测试通过。对应两个 XML 已随包提供，未修改 Agent 源码。

比较过输出历史 256（358400 字节）的整帧缓存配置：同样选择 200 对 / 20 fps 时，
只收到 119 对，约 11.92 Hz，待发送覆盖 80 对。最终保留 32 个 MTU 的有界连续分片，
相同硬件/主机配置下达到约 20 Hz，覆盖数为 0。

## 原始记录与产物

最终 micro-ROS ELF SHA-256：

```text
2ee2f6c8f23b9de6976be917997d979d2345d4f17cd7b5af6e0fc564ec06ae34
```

大小：6034696 字节。重建最终 32-MTU 配置后，与通过图像测试的二进制哈希一致。
包名：`schurvins-gcc11-static-micro-ros-a53.tar`。

原始文件位于 SDK 忽略目录 `install/micro-ros/`：

- `calibration-mock-small/`：7.5 fps 已知像素测试。
- `calibration-final-5/`、`calibration-real-20-small-stream/`：真实双目与 PGM 样本。
- `calibration-final-10-restart/`：重连消息、事件时间线、原始 VI 时间戳 CSV。
- `calibration-options.log`：开关、同步要求、Agent 缺席、SIGINT 检查。
- `calibration-pose-regression/`：原位姿通信回归。
- `calibration-vio-regression.log`：独立 VIO 位姿 CSV 一致性、线程和 socket 检查。
- `image-build-final.log`、`calibration-build-final.log`：最终构建记录。

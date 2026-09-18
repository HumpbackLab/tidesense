# TideSense micro-ROS 验证记录

日期：2026-09-18。测试设备：`root@192.168.123.99`，SG2002 A53。
目标程序使用 SDK GCC 11.3.1 全静态链接，六项已有 NEON 优化均开启。
这是通信集成与回归测试，不是实际 VIO 精度或长期运行测试。

## 构建与开关

- `SCHUR_WITH_MICRO_ROS=OFF`、`ON` 均编译、打包成功。
- 两个 ELF 均无 `PT_INTERP`、无 `DT_NEEDED`。
- micro-ROS 编译开启的程序仍默认关闭通信。
- 关闭时测试进程只有一个线程，无 socket；开启时增加一个通信线程。
- Agent 不存在时，VIO 能处理完输入并正常退出。
- 未编译 micro-ROS 却请求启用、未提供 Agent 地址、非法启用值，均明确报错。
- 在不含系统动态库的独立 chroot 中，以数字 IPv4 启用通信并运行 CSV 回放通过。
  XRCE POSIX transport 的 `getaddrinfo` 会产生静态链接告警；本接口只接受数字 IPv4，
  上述测试验证了实际调用路径不依赖板上 NSS/glibc 动态库。

## 估计结果回归

相同 6 帧合成双目/IMU 输入，以下四种模式均为 `tracking=6, imu_fused=6`：

1. 不包含 micro-ROS 的独立版。
2. 包含 micro-ROS，但保持默认关闭。
3. `SCHUR_MICRO_ROS=0` 显式关闭（同时给出无效 Agent 地址，确认关闭时忽略通信配置）。
4. `SCHUR_MICRO_ROS=1`，但 Agent 缺席。

四份位姿 CSV 完全相同，SHA-256：

```text
78d344aa84d8b61b6913d20fddea3f6f22dd5d8a92740c3e0db996cfcbd70dac
```

另一个 100 帧合成回放中，连接 Agent 的程序与独立版位姿 CSV 也完全相同：

```text
3ad7b01bbe50331b33e6d727b587717a34b624a868590817c50eb51b34db0b63
```

测试发现并修复了上游 RMW 的随机数副作用：`rmw_init_options_init()` 原本调用
全局 `srand()/rand()`，会改变 SchurVINS 的 `std::random_shuffle()` 结果。
`0001-isolate-client-key-rng.patch` 改用局部 `rand_r` 状态；构建脚本校验并应用补丁。

## 实际网络收发与恢复

连接：板端 RNDIS → 电脑 `192.168.123.2`，UDP 18888，ROS domain 23。
电脑运行未经修改的 Humble micro-ROS Agent，ROS 接收端使用 Cyclone DDS。

- Agent：`c93ee764e0d2ef4907aeb29233c68cb5f4b56976`。
- Agent 内部 XRCE Agent：2.4.2；Fast DDS：2.6.10。
- ROS 接收端：`rmw_cyclonedds_cpp` 1.3.5，Cyclone DDS 0.10.5。
- 客户端各源码精确版本见 `sources.repos`。

板端先运行 4 秒，再启动 Agent；12 秒后停止 Agent；间隔 4 秒再启动。
整个过程中不重启板端测试程序：

| 检查 | 结果 |
| --- | --- |
| 首次连接收到位姿 | 88 条 |
| 重启 Agent 后收到位姿 | 158 条 |
| 位姿总数 | 246 条 |
| 跟踪状态 | true 246 条，false 64 条 |
| 首次收数距 Agent 启动 | 约 1.23 秒 |
| 恢复收数距 Agent 重启 | 约 0.49 秒 |
| ROS 节点发现 | `/tidesense/tidesense` |
| 已知位姿、四元数、时间戳、frame_id 校验 | 全部通过 |

数量包含发现过程和停机期间的丢弃，不承诺每个 VIO 输出都送达；这是实时
best-effort 状态通道。上述恢复时间是本次测试结果，不是最坏时延保证。

另外，真实 native SchurVINS 入口的 100 帧合成回放已向 ROS 发布位姿，
订阅端逐条按采集时间戳与本地 CSV 对齐检查，误差仅为 CSV 小数截断量级。

## 主机兼容性限制

当前主机的 `rmw_fastrtps_cpp` / Fast DDS 2.6.10 在多次测试中出现首次会话
发布端未匹配订阅者的情况：话题可见，Agent 收到并写出 DDS 数据，但 ROS 无消息，
Agent 重启后恢复。可靠性切换、UDP-only 与单播发现配置没有解决该环境的问题。
改用 Cyclone DDS 接收端后，首次连接、重连和 native 数据校验均通过。
因此使用说明推荐本次已验证的 Cyclone DDS 接收端配置；不宣称所有 Fast DDS
版本都有此问题。排障时加入 Agent 的诊断代码已撤回，没有发布修改版 Agent。

## 产物与原始记录

产物目录：`install/soc_sg2002_tidesense_arm64_sd/`。

| 产物 | 字节数 | ELF SHA-256 |
| --- | ---: | --- |
| 独立版 `schurvins-gcc11-static/bin/schurvins_stereo` | 5800448 | `073f8b16ec10fa5f18fb792073cfbd58c5612ba0ccbe4064759d0d8070643c09` |
| 可选 micro-ROS 版 `schurvins-gcc11-static-micro-ros/bin/schurvins_stereo` | 6014136 | `0165411e7949e9be0b4fd8601694991d19faea0fbe269d5b79ce19197f6b3362` |

原始日志保存在 SDK 忽略的 `install/micro-ros/`：

- `board-option-checks-fixed.log`：关闭、开启但 Agent 缺席、输入错误与线程/socket 检查。
- `board-chroot-final.log`：无动态库的 chroot 测试。
- `board-native-final.log`：100 帧位姿 CSV 一致性。
- `board-transport-cyclone/`：消息、事件时间线、Agent 日志、板端日志和汇总。
- `main-integration/`：native VIO 的 ROS 消息与 CSV 对齐结果。

可复现的测试入口见 `../tests/board-micro-ros.py`、`../tests/micro_ros_receiver.py`
和 `../tests/board-micro-ros-options.sh`。合成 CSV 数据由 `../make-smoke-data.py` 生成。

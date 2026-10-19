# README 设备端命令验证（2026-10-19）

设备：`root@192.168.123.99`，TideSense SG2002 A53。
本次实际执行命令并保留输出，未将历史验证记录直接当成本次通过。

## 范围与执行方式

修改前从根 `README.md` 递归追踪本地 Markdown 链接，共 51 份文档、193 个代码块；
无失效的本地 Markdown 文件链接。完整快照、各文档覆盖说明及原始日志保存在工程根目录的
`install/readme-validation-20261019/`，其中 `document-inventory.json` 保存修改前的代码块及行号，
`coverage.json` 逐文档说明覆盖范围，`results.jsonl` 保存各次执行的退出码和耗时。
每项测试的同名 `.sh` / `.log` 分别保存实际命令和完整输出。

板端程序串行使用相机和 IMU。示例中的 `CALIB.yaml`、`actual-calibration.yaml`、
`/path/dataset-export`、`your_program`、`PID` 等占位符替换为随包本板标定、本次实录数据和实际进程。
持续运行的主要 VIO 示例按文档后台启动并以 SIGINT 正常停止；其他采集示例使用文档支持的有限帧数或时长。
解包测试写入独立验证目录。电脑端编译、RViz2、重新求解标定、刷 SD 卡和历史回退命令不属于板端运行通过项。

## 环境及前置条件

- 内核：`5.10.4-tag--gb4f72fbbb23a`，构建日期 2026-10-19。
- `/mnt/data`：ext4，`rw,relatime`，没有 `sync`；约 7 GiB 可用数据分区。
- A53 / MPLL：`1000000000` Hz；初始温度约 38.5°C。
- 设备初始墙钟为 1970 年，已在 ROS 测试前校时；应用不自动同步电脑和设备时钟。
- SchurVINS 默认部署目录为 `/mnt/data/schurvins-gcc11-static-micro-ros/`，编译启用 micro-ROS，
  源码版本记录为 `07149fe60aa4b514ead14d687fabb024001fbed9`。
- 设备 `schurvins.yaml` 与仓库 `scripts/schurvins/tidesense.yaml` SHA256 一致：
  `d5dc15c8008cc49be5379017c5271c894735f51627ca499fec32c349d9c91455`。
- 本次实测速率约相机 **20.394 Hz**、IMU **203.94 Hz**；20/200 Hz 是标称值，没有重采样。

## 本次执行结果

下表日志名均相对于 `install/readme-validation-20261019/`。

| 文档中的板端用法 | 本次结果 | 日志 |
| --- | --- | --- |
| SchurVINS `./run.sh --help` | 返回 0，显示 `micro-ROS compiled: yes` | `04-help.log` |
| 包装脚本及直接二进制 `--self-test synthetic.yaml` | 各 6/6 帧跟踪、IMU 融合，返回 0 | `05-selftest.log`、`06-selftest-direct.log` |
| 合成 `stereo.csv` / `imu.csv` 输入 | 6/6 帧跟踪和融合，返回 0 | `12-csv-smoke.log` |
| `--imu-test auto 10` | 2040 条，203.946 Hz，最大间隔 4.921 ms，无软件丢样，返回 0 | `07-imu.log` |
| `--camera-smoke ... 120` | 120 对真实双目输入，通过 | `08-camera-smoke.log` |
| `--vio-smoke ... auto 120` | 120/120 帧跟踪和融合，IMU 无丢样，返回 0 | `09-vio-smoke.log` |
| `SCHUR_TEST_PROCESS_DELAY_MS=100 ... --camera-smoke ... 80` | 80 帧过载测试通过，最新帧替换正常 | `10-camera-overload.log` |
| 实际标定 `--camera schurvins.yaml ... 120` | 纯视觉运行返回 0 | `13-pure-visual.log` |
| 实际标定 `--vio ... --record NEW_DIR` | 180 帧全部跟踪和融合，保存 3580 条 IMU，生成 `COMPLETE`，返回 0 | `14-live-record.log` |
| `--replay`、原 CSV 入口、`--allow-incomplete` 选项 | 各返回 0；后者在本次完整录制上验证选项用法 | `15-replay.log`、`16-replay-csv.log`、`39-replay-allow-incomplete.log` |
| 独立 dataset 录制 10 秒 | 205 对、2071 条 IMU，最大 IMU 间隔 4.949 ms，队列峰值 3/64，生成 `COMPLETE` | `17-dataset.log` |
| 取回并导出 EuRoC / ROS1 bag / SchurVINS replay | 导出完整性检查通过；板端回放 205/205 帧跟踪和融合，返回 0 | `32-dataset-export.log`、`38-dataset-replay.log` |
| `--calibrate ... --fps 5` | 选择并发送 50 对，返回 0 | `34-calibration-5hz.log` |
| `--calibrate ... --fps 10 --frames 300 --timestamps ...` | 300 对，时间戳 CSV 生成，返回 0 | `35-calibration-10hz.log` |
| `--calibrate ... --fps 20 --imu auto` | 选择 200 对、发送 184 对；读取并发布 2040 条 IMU，返回 0；投递损失见下文 | `36-calibration-imu.log` |
| `nohup ... --vio`、预测 CSV、运行中 `fsin.sh status` / `diagnostics`、`kill -INT` | 正常启动与停止；电脑收到全部 4606 条预测位姿，约 203.94 Hz | `37-vio-micro-ros.log`、`schur-received.json` |
| `perf stat` 硬件事件 | cycles / instructions / cache-misses / branch-misses 均计数，返回 0 | `18-perf-hardware.log` |
| `perf stat -p PID ... -- sleep 10` | 附加已有进程成功，返回 0 | `19-perf-attach.log` |
| `perf record ... --call-graph fp` / `perf report --stdio` | 287 样本、丢失 0，能解析 `mix → workload → main` | `20-perf-record.log`、`21-perf-report.log` |
| `perf top --stdio -e cycles` | 输出实时热点并以 SIGINT 退出，返回 0；交互命令应在终端使用 | `22-perf-top.log` |
| `perf stat` 软件事件 | task-clock / context-switches / page-faults 正常，返回 0 | `23-perf-software.log` |
| `fsin.sh prepare/start 20/status/stop` | 内核触发接口正常，返回 0 | `24-fsin-tools.log` |
| OV9281 `mode_check` | 四种配置及非法输入回调检查通过 | `25-mode-check.log` |
| `capture ... --fsin-test` | 每目 600 帧，约 20.394 Hz；触发前、停止预热后、有限脉冲结束后均无新帧 | `26-fsin-capture.log` |
| `OV9281_FSIN_MAX_EXPOSURE=1 ... --fsin-test` | 每目 600 帧，两路曝光均限制为 546 行，返回 0 | `27-fsin-exposure.log` |
| 640×400 / 640×480 / 1280×800 自由运行示例 | 每目各 600 帧，三个命令均返回 0 | `28-free-400.log`、`29-free-480.log`、`30-free-800.log` |
| 相机 IPC / dataset FIFO 的 mock 测试 | 像素所有权、队列、坏头、截断、时间倒退及清理检查通过 | `40-camera-protocol.log`、`41-dataset-protocol.log` |
| 预测、单级 IMU 内参、录制测试程序 | 各返回 0 | `42-prediction-test.log`、`43-intrinsics-test.log`、`44-recording-test.log` |
| SchurVINS / dataset tar 解包及包内 help | 隔离目录解包成功，两个包均可执行 | `48-tar-packages.log` |
| OpenVINS 完整 FP64 核心及实时入口构建、完整部署、help | 构建及部署成功，help 返回 0 | `openvins-core-build.log`、`openvins-live-build.log`、`49-openvins-help.log` |
| `OPENVINS_MICRO_ROS=0 ... run-live.sh` | 240 帧输入、无 IMU 丢样，正常退出；但初始化为 0，不能判定 VIO 成功 | `50-openvins-independent.log` |
| OpenVINS `nohup ... run-live.sh`、两种 `tail -f`、`kill -INT` | 启动、查看日志及清理正常；247 帧均未初始化，未发布位姿 | `51-openvins-micro-ros.log` |
| OpenVINS 电脑端 pose / tracking 订阅 | 收到 247 条 `tracking=false`，0 条位姿；pose 单条订阅超时，tracking 单条订阅成功 | `openvins-received.json`、`cli-pose.log`、`cli-tracking.log` |
| 修订后的 `perf --buildid-dir /mnt/data/perf-cache record/report` | 指定 DATA 符号缓存的采样及解析均返回 0 | `55-perf-data-cache.log` |

## 发现并修正的文档问题

1. `CAMERA.md`、`IMU.md` 使用旧的 `/mnt/data/schurvins-gcc11-static`，新镜像不存在该目录，
   原命令 `cd` 返回 2。现改为默认带 micro-ROS 的路径，并说明独立包路径。
2. OV9281 文档假设 `capture` 和 `mode_check` 已部署，但默认镜像只有 `fsin.sh`，原命令返回 127。
   已补充电脑端构建及复制步骤；本次部署后完成表中的真实相机验证。
3. OpenVINS 不在默认 SD 镜像内，原启动示例 `cd` 返回 2；仅复制主程序及配置不足以首次部署。
   已说明安装前提并补齐目录、启动脚本、辅助程序和配置的完整部署步骤。
4. micro-ROS 子文档的主机命令没有统一使用当前环境脚本和本机 DDS XML，
   现与根 SchurVINS README 一致，并使用 `--no-daemon` 的订阅方式。
5. 板级 README 仍声称 IMU 未接入 IIO，且将当前 FSIN 描述为 PWM8；已改为当前的 IIO / INT1 分频实现。
   相机子文档中当前曝光及 YNR 描述也与随包的 0.5 ms / 192 对齐，历史记录明确保留为历史参数。
6. perf 示例虽然将 `perf.data` 放到 DATA，默认符号缓存仍写入 `/root/.debug`，
   本次实际占用约 61 MiB ROOTFS。已在采样、报告和热点命令中增加
   `--buildid-dir /mnt/data/perf-cache`；本次缓存也迁移保留到该目录。

初次失败的输出保存在 `01-historical-path.log`、`02-openvins-preinstalled.log`、`03-ov9281-preinstalled.log`。
日志 `31-fetch-captures` 的非零退出发生在测试用的多远端源拷贝中；拆成单独的 scp 后成功取回，
导出完整性检查通过。该次属于验证脚本的问题，不计为文档单源 scp 示例失败。

## 运行边界与未通过项

- **OpenVINS 位姿验证未通过**：独立运行处理 240 帧，带 micro-ROS 运行处理 247 帧，
  均 `initialized=0`。本次导出的左目图像主要呈噪声、缺少可识别场景纹理；
  日志出现 `not enough feats to compute disp`，曝光约 494µs，增益已较高。
  程序能采集和清理，且成功连接 Agent，但没有位姿可接收。
  主程序在无初始化时也可能返回 0，故本报告按初始化标志与收到的位姿数判定。
  应调整实际光照、画面纹理并保持静止后复测；本次未改标定或算法阈值。
  这也意味着 SchurVINS 在该画面上的 `tracking` 只能证明流程可执行，不能证明真实定位有效。
- 20 Hz 标定联合发布：本次选择 200 对，发送线程实际处理 184 对，报告 16 次替换/繁忙；
  三种标定模式合计电脑收到左右各 534 张，左右时间戳逐项相同；IMU 订阅端收到 1912 条。
  此接收器使用 best-effort QoS，不能把应用返回 0 或 `queued` 当成全量接收证明。
- 现场及两个回放入口的 180 个输出时间戳全部相同、位姿均有限，但相对现场最大位置差分别为
  **0.01773 m** 和 **0.02208 m**，最大四元数分量差分别约 **0.001095** 和 **0.001615**。
  本次只验证录制/回放可用，不声称逐位复现；统计见 `replay-comparison.json`。
- 高频位姿链路：预测 CSV 和 ROS 实收各 4606 行，时间戳映射全部匹配，
  位姿分量最大差 `5.56e-16`；见 `prediction-transport-comparison.json`。
- 递归链接中的历史 NEON、profile、VINS-Fusion、OpenVINS 离线、LK / FP32 / codec-MV 实验依赖
  当前新镜像没有的旧程序、配置及数据，例如 `/mnt/data/schur-opt-test`、
  `/mnt/data/vio-record-20260924-121320`、`/mnt/data/vio-compare/backend-f32` 和
  `/mnt/data/venc-h264-direct-20261017`。已核对缺件并记录 `45-historical-prerequisites.log`，
  **未执行这些原始历史实验，不能标记为命令全部通过**。逐文档说明见 `coverage.json`。
- 未执行重刷整卡、内核/FIP 回退、强制重启、卸载 DATA 后手动扩容；这些属于维护操作，
  不作为当前运行命令的测试步骤。已核对三分区及首次扩容备份文件存在。
- 本次未重新标定、未人工完成运动采集、未做定位精度或长期稳定性评估。
- 整轮测试后的驱动累计诊断为 `spi_errors=0`、`buffer_drops=0`、`late_reads=4`、`gaps=10`，
  不能将应用层未丢样概括为整个压力测试期间驱动无异常间隔。
  最终 A53 仍为 1GHz、温度约 40.6°C；完整诊断及内核日志见 `54-final-device-state.log`。

## 测试产物与设备状态

板端本次产物集中在 `/mnt/data/readme-validation-20261019/`。
本机原始数据、导出的约 101 MiB ROS1 bag、位姿 CSV、接收记录和执行日志位于
`install/readme-validation-20261019/`，该构建目录不纳入 Git。
测试所需的 OpenVINS 和 OV9281 工具保留在各自文档指定的部署目录。
perf 符号缓存保存在 `/mnt/data/perf-cache/`，不再占用 ROOTFS 的 `/root/.debug`。
最终没有遗留 VIO、相机采集或 IMU 测试进程，IIO buffer 和 FSIN enable 均为 0；
本次启动的电脑端 micro-ROS Agent 已停止。设备可重新用于正常采集。

# 联合模式板端验证（2026-10-24）

设备：192.168.123.99，SG2002 Linux A53 单核，双目 OV9281 原图 640×400，SC7U22 实测约 203.94 Hz，FSIN 双目约 20.394 Hz。所有数字均为板端实测；目标 20 Hz 不等于任意场景下保证持续输出有效位姿。

## 正确性

- TPU 2×2 unsigned 平均池化：双目 640×400→320×200，常量 0/255、随机图像等逐像素对照 OpenCV INTER_AREA，原图保持不变；约 1.2 ms（无 VIO 竞争）。
- TPU SAD 质量元数据：在已验证的精确最优代价/视差之外，逐像素对照 CPU 全代价体中的 d−1、d+1 及排除最优视差 ±1 后的最小代价。320×200/D32 的已知平面，以及非整齐尺寸 97×57/D7 的随机、平坦、极值和遮挡样例通过。
- `joint_depth_test`：已知 50 mm 基线、相机相反朝向、原图 16 px 视差，降采样后恢复约 1000 mm 深度；中心有效覆盖超过 20000 像素，平坦和重复歧义纹理全部拒绝，图像不被改写。
- 减小分辨率后使用 `(u−0.5)/2` 映射标定源坐标；快速双线性矫正与 OpenCV INTER_LINEAR、NEON XSobel 与 OpenCV Sobel 逐像素比对。
- 原 CPU 深度模式的接口默认仍使用原图矫正；原几何及毫米转换回归测试另外执行。
- micro-ROS 使用单个上下文、三个 publisher；电脑接收器严格按时间戳关联 Image、CameraInfo 和 PoseStamped，并检查图像长度、单位、内参、四元数和时间戳递增。

## 性能

首次将两条 CPU 图像处理分支直接并行运行，完整 VIO 参数下只有约 8 Hz。perf 的 `cycles:u` 采样定位到 FAST 检测 17.79%、非极大值抑制 13.00%、FAST 打分 10.35% 和 FAST detector 包装层 5.14% 等热点；该采样来自中间配置，不能直接当作最终配置的占比。

CPU 图像处理改为顺序调度，只让 VIO 与 TPU 匹配重叠；补充精确的 NEON XSobel 和固定图的 mono8 双线性映射。将特征检测起始层改为 1 后，原 120 特征/240 seeds/5 keyframes/4 window 的无 ROS 运行达到 17.28 Hz。

以下为 400 帧实时参数测试，Agent 在 192.168.123.2:18892，domain 25，正常启用质量门限：

| 配置 | 板端处理 Hz | 主机深度接收 Hz | 主机有效三话题配对 Hz |
| --- | ---: | ---: | ---: |
| 80 features / 120 seeds / 3 keyframes / 3 window / FAST10 | 19.33 | 19.31 | 12.62 |
| **60 features / 90 seeds / 3 keyframes / 3 window / FAST15（当前默认）** | **20.04** | **20.04** | **13.44** |

最终参数测试：板端 400 帧全部发布，ROS 发送队列无覆盖，主机收到 399 张深度图（首帧连接/发现期间未收到），269 个位姿，268 组完整三话题配对；校验错误 0。平均每帧处理 45.16 ms；原始相机序列跳过 7 帧，101 帧处理时间超过 50 ms，因此这不是硬实时保证。深度采集间隔中位数 49.036 ms、P99 98.026 ms。

当前场景中，VIO 多次报告 Insufficient，深度有效像素也很少。程序仅在 IMU ready、stage Tracking、质量不为 Insufficient、帧时间戳与当前采集一致时发布有限位姿；不会重复旧位姿，也不会把质量不足的结果包装成有效 20 Hz 输出。**当前证据仅证明深度和联合处理约 20 Hz，尚未证明持续的有效位姿＋深度 20 Hz。** 需要在明亮、有稳定纹理、可观察近景深度的场景下进一步复测跟踪、频率和运动精度。

默认预算相对原 VIO 减少特征、seeds、关键帧及窗口，并跳过第 0 层特征提取；尚无轨迹精度和长时间稳定性结论。深度通过数值及合成几何验证，尚无真实测距误差评估。

本机原始证据位于忽略的 `install/joint-validation-20261024/`，包括板端 CSV/日志、两组 ROS 接收 JSON/PNG、perf 数据及中间可符号化程序。运行方式见 README，可通过 `--profile` 和 `receive.py --require-hz 19.5` 重复验证；后者检查的是有效配对频率，不会把仅有深度的帧计为成功。

## 2026-11-02 SD 发布构建修复

[v0.0.3 发布 CI](https://github.com/HumpbackLab/tidesense/actions/runs/41375133377)
在联合模式 CMake 配置时失败：引入 `scripts/schurvins` 后，新的独立 VIO 默认后端
`native-dds` 触发了 `find_package(ament_cmake REQUIRED)`。
联合程序实际只链接 `schurvins` 估计器库，并通过 `combined_ros.cpp` 使用自己的
micro-ROS 通信；独立 VIO 的 ROS 桥并不参与联合程序链接。本地原有 CMake 缓存为
`SCHUR_ROS_BACKEND=none`，因此旧构建目录没有暴露这一问题。

联合模式现在在引入子目录前将独立 VIO 后端缓存固定为 `none`，覆盖新默认值和旧缓存。
独立 SchurVINS 的默认原生 DDS 配置以及联合模式的 micro-ROS 发布行为不变。

本地验证使用已有交叉编译依赖，未从头重建完整 SDK，也未重新触发 GitHub 发布：

- 移除 ROS 环境变量后，在隔离目录成功复现原始 ament 查找错误；同一目录应用修复后配置通过。
- 新目录显式指定 `-DSCHUR_ROS_BACKEND=native-dds`，并禁止查找 ament/rcl，配置仍通过；
  最终后端为 `none`，联合目标链接估计器与 `libmicroros.a`，没有原生 DDS 动态库。
- 在 `SCHUR_ROS_BACKEND=native-dds` 环境下运行完整 `scripts/vio-depth/build.sh`，
  联合程序、`joint_depth_test` 及发布 tar 均构建成功；QEMU 执行联合程序 `--help` 通过。
  这是构建和程序加载检查，未执行需要真实 TPU 的运行测试。
- perf 构建、SD 应用安装的 13 个单元测试、原生 DDS 应用包安装检查均通过。
- 使用实际应用产物生成 BOOT/ROOTFS/DATA 三分区 SD 镜像，大小为 818937856 字节。
- 按发布 CI 使用 `xz -T2 -6` 压缩镜像，`xz --test` 和原始/压缩镜像的 SHA256 校验均通过。

日志与复现目录保存在工程根目录 `install/ci-sd-fix/`，包括原始 CI 日志、修复前后的
CMake 输出、交叉编译日志、应用安装检查及镜像生成日志。

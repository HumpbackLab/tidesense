# all64 与缓存取图（2026-10-23）

以下采集性能数据来自迁移到 `tide_cam` 前的相机 helper。当前深度程序已直接链接
`tide_cam`，旧 helper 的开关和复测命令不再适用；StereoBM 的 all64 验证仍适用。

深度程序默认启用 `all64`，共享相机 helper 默认启用缓存映射及逐帧 DMA 缓存失效。
默认请求频率由 10 Hz 提至 20 Hz。匹配范围和质量过滤参数保持原配置，输出仍为
毫米 `16UC1`，并配套同时间戳的 CameraInfo。

## 实现

- `build-stereobm.py` 复用此前审计和验证过的 `prepare-opt.py`，锁定 OpenCV
  3.4.20 StereoBM 源码 SHA-256，再以 SDK 原有 OpenCV 编译参数生成 all64 对象。
  CMake 将该对象放在原 calib3d 静态库之前链接，只作用于深度程序。
  `all64` = 最多 64 行的串行分块 + NEON widening 更新 + 出窗差分重算。
- 不修改 SDK 已安装 OpenCV 静态库，因此 SchurVINS 的 OpenCV 链接行为保持原配置。
- 相机 helper 对取出的 Y 平面使用 `CVI_SYS_MmapCache`，读取前调用
  `CVI_SYS_IonInvalidateCache`；复制完成后解除映射并释放 VI 帧。
  缓存失效失败时明确退出并清理，避免静默发送旧缓存内容。
- `STEREOBM-BUILD.json` 记录源文件、生成文件和对象哈希、宏及完整编译命令；
  `BUILD-INFO` 标记构建配置。精确未剥离主程序和采集 ELF 保留在构建目录。

## 正确性验证

在 A53 上分别运行标准 OpenCV 与 all64 构建的 `verify_matcher`：

| 内容 | 对照数量 | 结果 |
| --- | --- | --- |
| 320×200 / 640×400 原始有符号 Q4 视差，D16/32/48/64，多种纹理、遮挡、噪声及平坦区域 | 120 | 全图 FNV64 指纹及有效像素数一致 |
| 同一组 32 对真实录制原图，通过当前 radtan 标定和深度有效性检查，分别生成两种分辨率深度 | 64 | 全图指纹及有效像素数一致 |
| 对同一张持有的 VI DMA 图像比较缓存/未缓存读取，覆盖连续帧及缓冲复用 | 214 张图像 | 逐像素相同，未发现旧帧或行损坏 |

120 组视差测试中 68 组含有效视差，64 张深度图中 30 张含有效深度，
所以对照并非仅比较全零图。几何测试在主机和 A53 均通过。
共享新 helper 的 SchurVINS `--vio-smoke ... auto 60` 也通过：
`frames=60 tracking=60 imu_fused=60`，IMU 队列无丢样；该检查仅验证输入链路。

## 分项性能对照

A53 1 GHz，双目 640×400 / 20.39 Hz 原始采集，深度输出 640×400 / D64，
请求 20 Hz，micro-ROS Agent 与真实 ROS 订阅器运行。
四组均使用同一支持缓存开关的 helper，分别切换匹配器与映射方式。
采样方法同 [基线报告](PERFORMANCE.md)，每组采样 18 秒，排除前 10 帧。

| 匹配器 / 取图 | BM CPU ms/帧 | 总计算 CPU ms/帧 | 总计算实际 ms/帧 | 稳态处理 Hz | 采集进程占核心 |
| --- | --- | --- | --- | --- | --- |
| 标准 / 未缓存 | 249.84 | 283.69 | 414.68 | 2.41 | 18.09% |
| 标准 / 缓存 | 248.44 | 282.63 | 362.40 | 2.75 | 8.21% |
| all64 / 未缓存 | 136.69 | 170.01 | 272.33 | 3.65 | 18.08% |
| all64 / 缓存 | 137.05 | 171.00 | 230.17 | 4.33 | 8.16% |

all64 使匹配 CPU 时间减少约 45%；缓存取图使采集进程的核心占用降低约 10 个百分点。
两项组合的采样期间处理速率提升约 80%。采样有额外开销，正式输出另行无 perf 测量。
全分辨率仍受计算量限制，不能达到 20 Hz；默认 320×200 面向较高输出频率。

## 正式程序输出（未运行 perf）

同一设备、Agent 和 ROS 订阅器下，分别运行升级前备份与最终优化包：

| 程序 | 输出尺寸 | 实际接收 Hz | 接收帧数 | 平均计算实际 ms/帧 |
| --- | --- | --- | --- | --- |
| 升级前，显式请求 20 Hz | 640×400 | 2.69 | 79 / 80 处理帧 | 371.39 |
| all64 + 缓存取图，默认请求 20 Hz | 640×400 | **4.61** | 100 / 100 处理帧 | 216.32 |
| all64 + 缓存取图，默认请求 20 Hz | 320×200 | **20.03** | 399 / 400 处理帧 | 29.19 |

全分辨率实测接收吞吐量提升约 71%。接收器使用固定测量窗口，首尾帧数可能与板端处理数不同；
三次接收均通过元数据、单调采集时间戳以及 Image/CameraInfo 配对检查。
320 模式无待处理图像覆盖；640 模式计算慢于采集，继续只保留最新待处理图像。

perf 复采中，计算阶段 BM 的周期占比由 88.28% 降至 81.03%，
校正相对占比由 11.06% 升至 17.87%，深度转换约 1%。这是匹配计算绝对耗时降低后的占比变化。

缓存改动位于 `scripts/schurvins/camera_source.c`，因为它是深度、VIO、dataset
共用的相机 helper，并非 VIO 估计器。以后重新构建这些包会共同采用缓存读取；
本次板端仅替换 depth 包，VIO 包仍使用原安装文件。VIO 回归通过环境变量显式指定新 helper。

## 构建与回退

```sh
# 默认开启
scripts/depth/build.sh
# 构建标准匹配器对照版本
DEPTH_STEREOBM_ALL64=OFF scripts/depth/build.sh
```

也可以用 CMake 在独立目录构建对照，避免覆盖正式运行包：

```sh
cmake -S scripts/depth -B install/depth-base-check \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/scripts/schurvins/a53-gcc11-toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$PWD/install/schurvins-gcc11-static/stage" \
  -DCMAKE_BUILD_TYPE=Release -DDEPTH_STEREOBM_ALL64=OFF \
  -DDEPTH_BUILD_TESTS=ON -DDEPTH_BUILD_APP=OFF
cmake --build install/depth-base-check -j4
```

将标准/优化两个 `verify_matcher` 复制到 A53，使用同一标定与同一
`FIXTURE_DIR/pairs.csv`（左右 PGM 路径，按行逗号分隔）运行并比较输出 CSV。
省略 FIXTURE_DIR 时仍会运行 120 组确定性视差测试。

```sh
verify_matcher calibration.yaml output.csv FIXTURE_DIR
SCHUR_CAMERA_CACHE=0 SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh
SCHUR_CAMERA_VERIFY_CACHE=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 ./run.sh --frames 100
```

首次诊断取图一致性后，应取消 VERIFY_CACHE 再测性能。
部署包在原路径 `/mnt/data/tidesense-depth`；升级前版本保存在
`/mnt/data/depth-opt-20261023/before`。
本次日志、指纹、原始 perf 记录、反汇编符号和实机接收统计保存在
`install/depth-opt-20261023/`，板端对应 `/mnt/data/depth-opt-20261023/`。

本次现场图像仍缺少清晰场景细节，测试不证明真实测距精度；更换场景后应复测质量和吞吐量。

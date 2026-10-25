# 根目录标定工具验证（2026-10-25）

## 独立性和数据流

- 入口：`calibration/calibrate.py`，运行时只导入同目录下的 `tide_calibration` 包。
  未调用 `scripts/calibration/kalibr.sh`、旧 bag 转换器或其他主机辅助脚本。
- 两次求解均使用 `/output/dataset.bag`，同一个宿主机文件单独只读挂载。
  结果文件均按 Kalibr 的 `dataset-*` 命名规则生成，无 bag 拷贝或再次转换。
- 测试将整个 `calibration/` 复制到临时目录，移除 PYTHONPATH，从其他工作目录执行：
  成功完成模拟两阶段求解与真实数据序列化/配置转换，临时环境没有 `scripts/` 目录。
- 开始整理前记录了旧录制/标定/VIO 目录下 360 个文件的 SHA-256，整理后全部一致。
  这些文件包含前序工作留下的未提交改动，本次未移动、删除、恢复或继续修改它们。

## 26 项测试通过

运行方式：

```sh
cd calibration
.venv/bin/python -m unittest discover -s tests -v
```

- 流程集成 10 项：广角/鱼眼参数贯穿、唯一 bag、实际图像/IMU/时间戳往返、旧标定快照忽略、
  镜头不匹配拒绝、独立目录运行、输入及输出检查、求解失败、环境不匹配、
  零退出码但缺少报告时拒绝成功，以及已移除选项拒绝。
  仅替换耗时的 Podman 求解边界，数据解码、ROS 1 写入和 SchurVINS 转换实际执行。
- 录制检查 7 项：640×400、640×480、1280×800 的未压缩及压缩图像逐字节核对；
  IMU SI 值、header 和 bag 时间；校验和/截断、未完成录制、元数据计数、
  相机参考号、IMU 间隔及首尾覆盖检查；旧十列 CSV 的原始 SI 列选择。
- 配置转换 9 项：外参方向、倒装相机、时间正负号及纳秒换算、噪声单位、
  完整尺度/轴不对齐逆模型与零偏坐标、三种尺寸和两种畸变模型、无效输入及覆盖已有配置拒绝。

测试中的噪声参数及求解结果仅用于软件验证，不用于产品标定。

## 真实容器验证

使用本目录的构建文件，通过 `--base localhost/tidesense-kalibr:noetic` 复用已有固定版本
Kalibr 编译产物，构建独立标签 `localhost/tidesense-calibration:noetic`，旧镜像保持原样。

- 新镜像 ID：`3aa51e084570ecbc148aa38bce09df09ce4136ad2efb18f101b8bd86a93b8b11`。
- Kalibr revision：`1f60227442d25e36365ef5f72cd80b9666d73467`。
- 构建时核对原版 `IccSensors.py` 的 SHA-256，将唯一的陀螺加速度敏感项激活点固定为零。
  预检确认标记、修改后源码校验和及求解器导入通过；运行时没有源码覆盖挂载。
- 预检最初发现 Kalibr 模块的导入顺序影响稀疏库符号解析，现已与官方 IMU 求解入口保持一致：
  `sm` → `kalibr_common` → `kalibr_imu_camera_calibration`。
- 对此前真实录制器采集的 640×400、61 对图像分别运行广角/鱼眼入口：
  两次均完成校验和唯一 bag 导出，进入真实 Kalibr 角点提取。
  图像不含 AprilGrid，均以缺少角点正确失败，保留 INCOMPLETE/日志，没有最终配置和 COMPLETE。
- 独立检查真实 IMU 求解入口：读取同一 bag、200 Hz IMU 和相机配置，进入问题构建及时间偏移初始化，
  最终因没有标定板观测而失败。未发生模型参数、容器依赖或动态库导入错误。

本次未重新编译完整 Kalibr C++，验证的是从已编译镜像构建新环境的路径。
也未完成新入口对真实标定板运动数据的两阶段优化或 VIO 精度验证。
产物位于忽略目录 `install/calibration-standalone-check-20261025/`。

Python 编译、shell 语法、文档文件链接和空白检查通过。

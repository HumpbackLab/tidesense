# TideSense

**TideSense 是一款基于 SG2002 的双目视觉惯性里程计（VIO）设备。** 通过同步采集双目图像和 IMU 数据，在板载 Arm Cortex-A53 上估计设备的位置与姿态，面向机器人、无人机和嵌入式视觉导航的开发与研究。

我们将开放 TideSense 的硬件设计与软件实现，希望让开发者能够从硬件搭建、传感器驱动、标定和数据采集，一直实践到板端 VIO 与性能优化。本仓库主要提供基于 SG2002 SDK 的板级适配、构建配置、应用程序和实验工具。

## 硬件配置

| 项目 | 配置 |
| --- | --- |
| 主控 | SG2002，使用 Arm Cortex-A53 运行 Linux 和 VIO |
| 双目相机 | 两颗 OV9281 全局快门图像传感器，设计基线 50 mm |
| 左相机 | I²C 7 位地址 `0x10`，MIPI CSI-2，2 lane |
| 右相机 | I²C 7 位地址 `0x60`，MIPI CSI-2，1 lane |
| IMU | SC7U22 六轴加速度计与陀螺仪，SPI 接口、INT1 数据就绪中断 |
| 启动与存储 | SD 卡启动，系统配置不依赖 SPI NOR / SPI NAND |
| 主机连接 | USB RNDIS 网络，设备端默认地址 `192.168.123.99` |

引脚分配见 [pin.md](pin.md)，芯片与传感器参考资料见 [docs/](docs/)。

## 硬件设计
硬件设计根据协议CC BY-NC-SA 4.0开源：
https://oshwhub.com/brics-hospital-drone-team/project_hpetjiuj


## 软件功能

- **同步采集**：当前采集配置为双目 `640×400` 灰度图像、20 Hz，以及 200 Hz IMU。内核在 IMU INT1 中断入口记录时间戳，每 10 次中断触发一次双目 FSIN。
- **板端 VIO**：适配 SchurVINS 和 OpenVINS，提供实时传感器输入、离线回放、位姿记录及 A53 交叉编译工具。
- **可选 ROS 2 接口**：通过 micro-ROS 与电脑端 Agent 发布位姿；可以关闭通信，独立运行 VIO。SchurVINS 支持 20 Hz 视觉校正与 200 Hz IMU 预测位姿输出。
- **独立双目深度模式**：板端 StereoBM 计算，micro-ROS 发布 `16UC1` 毫米深度及校正后的 CameraInfo；默认 320×200 / 请求 20 Hz，可选 640×400。启用 A53 匹配优化和缓存取图，与 VIO、UVC 分别运行。
- **标定工具**：提供 A4 AprilGrid、双目及相机–IMU 的 Kalibr 标定流程、IMU Allan 噪声分析，以及标定结果转换工具。
- **独立数据集录制**：不运行 VIO 即可录制双目和 IMU，保存原始时间戳并无损压缩图像；可导出 EuRoC 风格目录、ROS 1 bag 和 SchurVINS 回放数据，用于多种 VIO 算法对比。
- **性能分析与优化**：提供板端 perf 工具、前后端分项计时，以及 LK、SIMD、数值精度和硬件编码运动向量等实验代码与测量报告。实验功能的开关和适用范围见各自文档。

## 文档导航

以下文档中的电脑端命令均从工程根目录执行，工程内文件使用相对于根目录的路径；
板端命令会单独注明，并使用设备上的部署路径。

| 内容 | 入口 |
| --- | --- |
| SchurVINS 构建、运行与预测位姿 | [scripts/schurvins/README.md](scripts/schurvins/README.md) |
| OpenVINS 实时运行与 ROS 2 位姿发布 | [scripts/vio-compare/openvins/README-live.md](scripts/vio-compare/openvins/README-live.md) |
| 双目、相机–IMU 标定与参数转换 | [scripts/calibration/README.md](scripts/calibration/README.md) |
| 独立数据集录制、导出与回放 | [scripts/dataset/README.md](scripts/dataset/README.md) |
| micro-ROS 接入 | [scripts/schurvins/MICRO-ROS.md](scripts/schurvins/MICRO-ROS.md) |
| 双目深度输出与 ROS 2 预览 | [scripts/depth/README.md](scripts/depth/README.md) |
| 电脑端 RViz2 轨迹显示 | [scripts/visualization/README.md](scripts/visualization/README.md) |
| VIO 对比与优化实验 | [scripts/vio-compare/README.md](scripts/vio-compare/README.md) |
| 板端 perf 使用 | [scripts/perf/README.md](scripts/perf/README.md) |

欢迎围绕硬件、驱动、标定、算法适配和性能优化提交 Issue 或 Pull Request。复现问题时，请附上板卡版本、构建配置、标定参数和运行日志；运动估计问题也欢迎提供可回放的数据集。

---

# 快速入门指南

## 编译

- 获取源码：
``` bash
git clone https://github.com/HumpbackLab/tidesense.git
cd tidesense
./scripts/repo_clone.sh --gitclone ./scripts/subtree.xml
```

`tide_configurator/` 是独立的 GPLv3 submodule。`repo_clone.sh` 会在克隆清单中的子库后，
递归初始化主仓库及各子库的 submodule，并检出各自父仓库记录的提交。

- 更新所有子库（在已有工程的根目录执行）：

从 `scripts/subtree.xml` 读取子库路径，依次在各子库当前分支执行 `git pull`；某个子库更新失败时停止循环。此命令不更新根仓库。

```bash
while IFS= read -r repo; do
  printf '\n更新 %s\n' "$repo"
  git -C "$repo" pull || break
done < <(python3 - <<'PY'
import xml.etree.ElementTree as ET

for project in ET.parse("scripts/subtree.xml").getroot().findall("project"):
    print(project.get("path") or project.attrib["name"])
PY
)
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

- 编译

``` bash
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
clean_all
build_all
```

`build_all` 在编译及升级包打包成功后，会自动生成可直接烧录到 SD 卡的整卡镜像；镜像生成失败时，构建也会返回失败。镜像路径为：

```text
install/soc_sg2002_tidesense_arm64_sd/tidesense-sdcard.img
```

镜像包含 BOOT、ROOTFS 和 DATA 三个分区，首次启动会自动将 DATA 分区扩展至 SD 卡末尾。烧录步骤和单独重新打包命令见 [SD 卡镜像与烧录说明](build/boards/cv181x/sg2002_tidesense_arm64_sd/README.md#制作并烧录-sd-卡)。

`build_all` 默认构建并预装以下程序：

- `/mnt/data/schurvins-gcc11-static-micro-ros/`：SchurVINS，编译启用 `SCHUR_WITH_MICRO_ROS=ON`，附带 `schurvins.yaml`。
- `/mnt/data/tidesense-dataset/`：独立双目/IMU 录制工具。
- `/mnt/data/tidesense-uvc/`：USB 双目 MJPEG + IMU 模式。
- `/mnt/data/tidesense-depth/`：micro-ROS 双目深度模式，附带标定和电脑端接收工具。
- `/mnt/data/tidesense-vio-depth/`：[共享帧 VIO + TPU 深度模式](scripts/vio-depth/README.md)，640×400 SchurVINS 与 TPU 320×200 深度并行，经 micro-ROS 输出。
- `/usr/bin/perf` 及 `/mnt/data/perf/`：性能分析工具、测试负载和许可文件。

主机需安装 ROS 2 Humble 开发环境及各工具的构建依赖，首次构建会下载锁定版本的依赖。
micro-ROS 通信在运行时仍由 `SCHUR_MICRO_ROS=1` 和 Agent 地址启用。
随包标定来自当前测试板；更换硬件后应使用对应板卡的标定。

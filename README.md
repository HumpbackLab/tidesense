# TideSense

基于 SG2002 的双目视觉惯性里程计，在 Arm Cortex-A53 上运行 Linux 与 VIO。
本仓库提供板级 SDK、传感器驱动、标定、录制及 VIO 工具。

## 硬件

| 项目 | 配置 |
| --- | --- |
| 主控 | SG2002，Arm Cortex-A53 |
| 双目 | OV9281 全局快门，设计基线 50 mm |
| 左 / 右 | I²C 7 位地址 0x10 / 0x60，MIPI 2 / 1 lane，右相机倒装 180° |
| IMU | SC7U22，SPI，INT1 中断时间戳，200 Hz |
| 同步 | 每 10 次 IMU 中断触发双目 FSIN，名义 20 Hz |
| 存储 | 纯 SD 启动，无需 SPI NOR/NAND |
| 连接 | USB RNDIS，设备地址 192.168.123.99 |

引脚见 [pin.md](pin.md)，手册见 [docs/](docs/)。
硬件设计按 CC BY-NC-SA 4.0 开源：
https://oshwhub.com/brics-hospital-drone-team/project_hpetjiuj

## 功能与文档

| 功能 | 入口 |
| --- | --- |
| SchurVINS，20 Hz 视觉校正 / 200 Hz 预测 | [SchurVINS](scripts/schurvins/README.md) |
| OpenVINS，实时及离线对比 | [OpenVINS](scripts/vio-compare/openvins/README.md) |
| 独立双目/IMU 录制、导出 EuRoC / ROS bag | [数据集](scripts/dataset/README.md) |
| 双目与相机–IMU 标定 | [标定](scripts/calibration/README.md) |
| USB 双目 MJPEG + IMU | [UVC](scripts/uvc/README.md) |
| 毫米深度与 CameraInfo | [深度](scripts/depth/README.md) |
| 共享帧 VIO 与 TPU 深度 | [联合模式](scripts/vio-depth/README.md) |
| 网页预览与设置 | [TideConfigurator](tide_configurator/README.md) |
| 性能采样与硬件卸载实验 | [perf](scripts/perf/README.md)、[对比工具](scripts/vio-compare/README.md) |

ROS 2 通信按需启用，VIO 可独立运行。随包标定来自测试板，更换硬件后需重新标定。

## 编译

先准备 [开发环境](docs/development-environment.md)，电脑端命令均从工程根目录执行：

```sh
git clone https://github.com/HumpbackLab/tidesense.git
cd tidesense
./scripts/repo_clone.sh --gitclone ./scripts/subtree.xml
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
clean_all
build_all
```

克隆脚本会初始化主库及子库的 submodule，包括独立 GPLv3 项目 `tide_configurator/`。
构建使用已加载的 ROS 2，或通过 `TIDESENSE_ROS_SETUP` 指定 `setup.bash`；首次下载锁定依赖。
默认预装 SchurVINS、dataset、UVC、depth、vio-depth 和 perf，micro-ROS 运行时默认关闭。

整卡镜像：`install/soc_sg2002_tidesense_arm64_sd/tidesense-sdcard.img`。
包含 BOOT/ROOTFS/DATA，首次启动自动扩容 DATA；[烧录及重新打包](build/boards/cv181x/sg2002_tidesense_arm64_sd/README.md#制作并烧录-sd-卡)。
构建或整卡镜像生成失败时返回失败。

## 更新子库

以下只更新清单内子库当前分支，不更新主库；任一失败即停止循环：

```bash
while IFS= read -r repo; do
  printf '\n更新 %s\n' "$repo"
  git -C "$repo" pull || break
done < <(python3 - <<'PYCODE'
import xml.etree.ElementTree as ET
for project in ET.parse("scripts/subtree.xml").getroot().findall("project"):
    print(project.get("path") or project.attrib["name"])
PYCODE
)
git submodule sync --recursive
git submodule update --init --recursive --checkout
```

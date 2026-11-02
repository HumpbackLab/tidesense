# TideSense

[简体中文](README.md) | English

Stereo visual-inertial odometry based on the SG2002, running Linux and VIO on an Arm Cortex-A53.
This repository provides the board SDK, sensor drivers, calibration, recording, and VIO tools.

## Hardware

| Component | Configuration |
| --- | --- |
| SoC | SG2002, Arm Cortex-A53 |
| Stereo cameras | OV9281 global shutter, 50 mm nominal baseline |
| Left / right camera | 7-bit I²C addresses 0x10 / 0x60, 2 / 1 MIPI lanes; right camera mounted upside down (180°) |
| IMU | SC7U22 over SPI, INT1 interrupt timestamps, 200 Hz |
| Synchronization | One stereo FSIN trigger every 10 IMU interrupts, nominally 20 Hz |
| Storage | SD-only boot; no SPI NOR/NAND required |
| Connectivity | USB RNDIS, device address 192.168.123.99 |

See [pin.md](pin.md) for pin assignments and [docs/](docs/) for manuals.
The hardware design is available under CC BY-NC-SA 4.0:
https://oshwhub.com/brics-hospital-drone-team/project_hpetjiuj

## Features and documentation

| Feature | Documentation |
| --- | --- |
| SchurVINS, 20 Hz visual corrections / 200 Hz prediction | [SchurVINS](scripts/schurvins/README.md) |
| OpenVINS, live operation and offline comparisons | [OpenVINS](scripts/vio-compare/openvins/README.md) |
| Standalone stereo/IMU recording, EuRoC / ROS bag export | [Datasets](scripts/dataset/README.md) |
| Stereo and camera–IMU calibration | [Calibration](scripts/calibration/README.md) |
| USB stereo MJPEG + IMU | [UVC](scripts/uvc/README.md) |
| Depth in millimeters and CameraInfo | [Depth](scripts/depth/README.md) |
| VIO and TPU depth from shared frames | [Combined mode](scripts/vio-depth/README.md) |
| Browser preview and settings | [TideConfigurator](tide_configurator/README.en.md) |
| Performance profiling and hardware offload experiments | [perf](scripts/perf/README.md), [Comparison tools](scripts/vio-compare/README.md) |

Enable ROS 2 communication as needed; VIO can run independently.
The bundled calibration comes from the test board. Recalibrate when using different hardware.

## Build

Prepare the [development environment](docs/development-environment.md) first.
Run all computer-side commands from the repository root:

```sh
git clone https://github.com/HumpbackLab/tidesense.git
cd tidesense
./scripts/repo_clone.sh --gitclone ./scripts/subtree.xml
source build/envsetup_soc.sh
defconfig sg2002_tidesense_arm64_sd
clean_all
build_all
```

The clone script initializes submodules in the main repository and component repositories,
including `tide_configurator/`, a separate GPLv3 project.
The build uses the sourced ROS 2 environment, or the `setup.bash` specified by `TIDESENSE_ROS_SETUP`.
Pinned dependencies are downloaded on the first build.
SchurVINS, dataset, UVC, depth, vio-depth, and perf are preinstalled by default;
micro-ROS communication is disabled at runtime by default.

SD card image: `install/soc_sg2002_tidesense_arm64_sd/tidesense-sdcard.img`.
It contains BOOT, ROOTFS, and DATA partitions; DATA expands automatically on the first boot.
See [flashing and repacking](build/boards/cv181x/sg2002_tidesense_arm64_sd/README.md#制作并烧录-sd-卡).
The build returns a failure status if compilation or SD card image generation fails.

## Update component repositories

The following updates the current branch of each repository in the manifest, without updating
the main repository. The loop stops if any update fails:

```bash
while IFS= read -r repo; do
  printf '\nUpdating %s\n' "$repo"
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

# TideSense VIO comparison adapters

This directory contains the **version-controlled TideSense code** used to
cross compile and replay VINS-Mono, VINS-Fusion, and OpenVINS on the SG2002 A53.
The upstream projects are pinned in [sources.lock](sources.lock). Their full
checkouts, CMake build trees, static libraries, recordings, logs, and
trajectories live under `install/vio-compare-20260924-121320/`, which this SDK
ignores in Git. The detailed board results are in that directory's
[REPORT.md](../../install/vio-compare-20260924-121320/REPORT.md).

| Project | Maintained code here | Pinned upstream checkout in `install/` |
| --- | --- | --- |
| VINS-Mono | `vins-mono/standalone/` ROS-free left-camera/IMU adapter and CMake build, plus `export_tum.py` and board script | `vins-mono/upstream/` |
| VINS-Fusion | `vins-fusion/offline.cpp`, CMake build, ROS header shim, Kalibr converter, board scripts, trajectory tools, and [ROS removal patch](patches/vins-fusion-offline.patch) | `vins-fusion/VINS-Fusion/` |
| OpenVINS | `openvins/run_record.cpp`, Kalibr converter, A53 toolchain, board script, and [static offline build patch](patches/openvins-offline.patch) | `openvins/open_vins/` |

Upstream license notices remain in their respective checkouts. These adapters
are experiment code; they do not replace the SDK's SchurVINS production build.

## Sync and build

From the SDK root:

```sh
scripts/vio-compare/sync.sh
scripts/vio-compare/build.sh mono
scripts/vio-compare/build.sh fusion
scripts/vio-compare/build.sh openvins
```

`sync.sh` verifies the three pinned upstream revisions, applies the two patches
once, and copies the maintained adapter files to the ignored build workspace.
It downloads a checkout if it is absent and refuses to change a checkout at a
different revision. `build.sh` uses the SDK's GCC 11.3 AArch64 toolchain,
static OpenCV/glog from `install/schurvins-gcc11-static/stage`, Ceres 1.14 from
the comparison workspace, and for OpenVINS the cross-compiled Boost 1.74
libraries in `openvins/deps/`. Run `scripts/schurvins/build.sh` first if the
OpenCV/glog stage is missing. The comparison-specific Ceres and Boost stages
are preserved under `install/`; `build.sh` reports their absence rather than
silently using host libraries. `JOBS=2` is the default parallel build limit.

The VINS-Mono adapter currently embeds the camera and IMU values from
`cali/tidesense-imucam3-kalibr-20260924-101247` in `standalone/main.cpp` and
`standalone/parameters.cpp`. Rebuild it after changing those values. Generate
the other two project configs from a Kalibr result:

```sh
work=install/vio-compare-20260924-121320
kalibr=cali/tidesense-imucam3-kalibr-20260924-101247
python3 "$work/vins-fusion/generate_config.py" \
  "$kalibr/imucam-camchain-imucam.yaml" "$kalibr/imucam-imu.yaml" \
  "$work/vins-fusion/rotated" --rotate-right \
  --board-output-path /mnt/data/vio-compare/vins-fusion/rotated
python3 "$work/openvins/make_config.py" --kalibr-dir "$kalibr"
```

The TideSense right camera is rotated 180 degrees. VINS-Fusion and OpenVINS
rotate its PGM input and use a matching virtual-camera calibration. The
recording's stereo timestamps already include the cam0/IMU time offset, and
its IMU data already has six-face correction; neither is applied again.
VINS-Mono reads only the left camera.

## Replay artifact

The board copy is `/mnt/data/vio-compare/`. The replay scripts there read
`/mnt/data/vio-record-20260924-121320` and serialize runs through
`/mnt/data/vio-compare/run.lock`. This recording is marked `INCOMPLETE` due to
its writer queue, but its committed prefix has 744 complete stereo pairs and
8169 IMU rows. The source and output checks are in the ignored comparison
workspace's report; large recordings and trajectories are intentionally not
committed to the SDK.

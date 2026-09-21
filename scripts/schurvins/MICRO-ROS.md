# TideSense optional micro-ROS (ROS 2 Humble)

Board verification results: [micro_ros/VALIDATION.md](micro_ros/VALIDATION.md)
(packaged as `MICRO-ROS-VALIDATION.md`).

## Scope

The native SchurVINS process optionally links **rclc + rcl +
rmw_microxrcedds + Micro XRCE-DDS Client**. This is micro-ROS, not a custom UDP
protocol. A micro-ROS Agent on the companion computer exposes the node
`/tidesense/tidesense` and these best-effort, volatile topics:

| Topic | Type | Meaning |
| --- | --- | --- |
| `/tidesense/pose` | `geometry_msgs/msg/PoseStamped` | Tracked body/IMU pose `T_odom_body`, metres, quaternion xyzw; `frame_id=odom` |
| `/tidesense/tracking` | `std_msgs/msg/Bool` | Whether this processed frame has a valid pose; false suppresses pose publication |

Use a sensor-data/best-effort subscription (a reliable subscriber will not match).
PoseStamped has no child frame: its pose is the calibrated **body/IMU frame**, not
camera0. Mounting extrinsics to the vehicle's base_link must be applied on the
computer. No covariance or velocity is fabricated. No TF is published yet.

This first integration provides optional ROS telemetry. Keyframe images,
landmark messages, remote reset/parameters, map persistence and remote loop
closure are not implemented. No global correction is fed back into VIO.
The micro-ROS build limits entities to one node and three publishers; extend
`micro_ros/colcon.meta` when adding services/subscriptions. A publisher-only node
needs no rclc executor; all rcl/rmw calls are owned by the communication thread.

## Calibration images

The independent `--calibrate SENSOR.ini --fps HZ` mode publishes two reliable
`sensor_msgs/msg/Image` topics without running VIO. See [CALIBRATION.md](CALIBRATION.md)
for command lines, timestamps, recording and limitations. It requires the same
build/runtime opt-in. With `--imu auto`, a third reliable publisher sends `/tidesense/imu` at 200 Hz.
The image mode uses two publisher slots for left/right images;
ordinary VIO still uses them for pose/tracking. No images are sent by ordinary VIO.

## Build switches

Default build, **no micro-ROS library dependency**:

```sh
scripts/schurvins/build.sh
# Explicit equivalent:
SCHUR_WITH_MICRO_ROS=OFF scripts/schurvins/build.sh
```

Optional micro-ROS build (GCC11, A53, static):

```sh
SCHUR_WITH_MICRO_ROS=ON scripts/schurvins/build.sh
```

Requires host ROS 2 Humble development tools (default `/opt/ros/humble/setup.bash`,
override `MICRO_ROS_HOST_SETUP`), Python venv support, git, CMake and internet for
the initial fetch. `micro_ros/build.sh` creates a private Python environment under
`install/micro-ros`, downloads the exact commits in `micro_ros/sources.repos`,
and builds `install/micro-ros/firmware/build/libmicroros.a`. No host ROS library
is linked into the target. Host C/Python build generators run on the build PC.

The pinned RMW source receives `0001-isolate-client-key-rng.patch`: upstream
client-key initialization calls global `srand()/rand()`, also used by SchurVINS
feature shuffling. The patch uses a local POSIX `rand_r` seed, so starting or
reconnecting micro-ROS cannot reseed the estimator's random sequence.

The existing NEON environment switches still apply; their defaults remain OFF.
Outputs are separate, so an optional build does not replace the standalone VIO:

- Standalone: `install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static-a53.tar`
- Optional micro-ROS: `install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static-micro-ros-a53.tar`

For direct CMake use: `-DSCHUR_WITH_MICRO_ROS=ON` and optionally
`-DSCHUR_MICRO_ROS_ROOT=/path/to/firmware/build`.

## Runtime switches

**Even the micro-ROS-enabled binary starts with communication OFF.**
The original arguments and `run.sh` work unchanged:

```sh
./run.sh CALIB.yaml STEREO.csv IMU.csv POSES.csv
SCHUR_MICRO_ROS=0 ./run.sh --vio CALIB.yaml SENSOR.ini auto POSES.csv 0
```

Enable it explicitly, using the **computer's IP**, not TideSense's `.99`:

```sh
SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT=192.168.123.2 \
  ./run.sh --vio CALIB.yaml SENSOR.ini auto POSES.csv 0
```

| Environment | Default | Meaning |
| --- | --- | --- |
| `SCHUR_MICRO_ROS` | `0` | Exactly `0` or `1`; runtime opt-in |
| `SCHUR_MICRO_ROS_AGENT` | unset | Required numeric computer IPv4 when enabled |
| `SCHUR_MICRO_ROS_PORT` | `8888` | Agent UDP port, 1–65535 |
| `SCHUR_MICRO_ROS_DOMAIN` | `0` | ROS domain, 0–232; match host `ROS_DOMAIN_ID` |

`--help` reports whether micro-ROS was compiled. Requesting communication from a
standalone-only binary fails with an explicit build-switch instruction.
Smoke modes never publish a usable pose with their test-only calibration.
`--self-test` is synthetic data; do not feed its poses to navigation.

Disabled: no communication thread, ROS context, socket or dynamic ROS/image buffer allocation.
Enabled: fixed one-sample mailbox, nonblocking try-lock producer, latest sample
wins; the worker runs at nice +5. UDP best effort avoids reliable publication
waits. Agent absence/disconnect never waits inside the VIO processing thread;
the worker pings and reconnects in the background. Exit interrupts retry waits.
Counters on exit report connections, published poses and overwritten/busy samples.
There is no historical replay after an outage. Consumers must detect stale data;
a disconnected device cannot deliver a final `tracking=false` message.

## Agent on the computer

Use the Humble micro-ROS Agent. **For ROS 2 subscribers, use Cyclone DDS in the
currently tested host environment** (`ros-humble-rmw-cyclonedds-cpp`). The Agent
itself still uses Fast DDS internally; the two DDS implementations interoperate.

On the companion computer, install that ROS package if it is not already present:

```sh
sudo apt install ros-humble-rmw-cyclonedds-cpp
```

The existing host Fast DDS 2.6.10 receiver exhibited an initial writer-matching
failure: it discovered the topic but received no samples until the Agent was
restarted. Changing reliability or multicast settings did not resolve it.
Cyclone DDS received native VIO samples on the first connection. This is a
measured host compatibility limitation, not a claim about all Fast DDS versions.

This SDK also provides `scripts/schurvins/micro_ros/build-agent.sh` to build a
pinned host Agent after the micro-ROS library build. Its workspace setup is
`install/micro-ros/agent-install/local_setup.bash`.

```sh
source /opt/ros/humble/setup.bash
source /path/to/agent_ws/install/setup.bash
ROS_DOMAIN_ID=0 ros2 run micro_ros_agent micro_ros_agent udp4 -p 8888
```

Inspect from another terminal using the same ROS domain:

```sh
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_DOMAIN_ID=0
ros2 daemon stop  # discard a CLI daemon previously started with another RMW
ros2 node list
ros2 topic echo /tidesense/pose geometry_msgs/msg/PoseStamped --qos-reliability best_effort
ros2 topic echo /tidesense/tracking std_msgs/msg/Bool --qos-reliability best_effort
```

The transport is UDP/IP over the existing USB RNDIS connection. Allow UDP 8888
on that interface. Do not assume the computer always has `192.168.123.2`.

## Timestamps and coordinate resets

File replay preserves dataset acquisition timestamps (including the original CSV
output). It is not paced and does not publish `/clock`; recorded historical
stamps will not match the host's current wall time.

Live camera/IMU acquisition uses a common monotonic domain. For ROS publication
only, the app samples `CLOCK_REALTIME - CLOCK_MONOTONIC` once at startup and adds
that offset to the image acquisition timestamp. This requires a correctly set
board clock; it does **not** synchronize the two computers. Synchronize clocks
before starting live VIO. Clock jumps during a run are not applied to the offset.

The odom frame is local to a VIO run and can restart on estimator reset. This
initial interface does not provide a trajectory/session ID or a reset event;
consumers must not concatenate separate runs into a global map. Such identifiers
belong in the future keyframe/loop-closure protocol.

## Transport checks

Configure `-DSCHUR_BUILD_MICRO_ROS_TESTS=ON` to build
`schurvins_micro_ros_test`, which needs no camera/IMU and sends known poses plus
periodic tracking loss. On the computer, run (in a ROS 2 environment):

```sh
python3 scripts/schurvins/tests/micro_ros_receiver.py --seconds 45 --output /tmp/micro-ros-test.json
```

On the board, run the fixture with `SCHUR_MICRO_ROS=1` and the Agent address.
Starting the Agent late and restarting it during the 40-second fixture checks
recovery without restarting the board process. The receiver asserts pose values,
acquisition timestamps, frame IDs, tracking loss and ROS node discovery.

`tests/board-micro-ros.py` automates that fixture test using a dedicated domain 23
and port 18888. It takes `--binary`, `--agent`, `--output` and an optional
`--host-ip`; set `SSHPASS` and source the host ROS/Agent workspace first. The
default receiver RMW is Cyclone DDS; `--rmw` selects another installed RMW for
compatibility testing. The script waits for the ROS subscriber to be ready before
starting the board fixture.

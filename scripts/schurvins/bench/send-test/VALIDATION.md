# send-test 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

### Method

* Same binary, 501 stereo/IMU frames, all six NEON optimizations enabled.
* Four runtime modes: no sender, pose on every tracked frame, pose plus camera-0
  image at 2 Hz or 5 Hz **of the dataset timestamps** (roughly every 10/4 frames).
  Actual wall-clock image rate is different because this is unpaced replay.
* Mode order: baseline, pose, image2, image5, image5, image2, pose, baseline.
* 64 stereo pairs are loaded before each compute batch. This excludes SD read
  waits from step timing and causes sender/VIO overlap during continuous compute.
  Loading between batches is still present; this is not a live-camera benchmark.
* Both VIO-only time and compute-step time (IMU feed, VIO, pose extraction,
  synchronous packet submission) are recorded. Step excludes batch prefetch and
  final CSV writing. These are separate from whole replay wall time.
* Initial 5 frames are excluded from summary; raw data and a skip-50 sensitivity
  metric are retained. Compare identical pose CSV hashes and tracking counts.
* 4 preallocated 1 MiB SPSC slots; producer never waits on network. Sender thread
  has nice +5 and blocks on a semaphore when empty. Baseline creates the same
  buffers and thread, which remains blocked; this controls allocator/thread setup
  effects. Wakeup cost is included. No compression, DDS, retransmission, or
  feature packing.
* UDP carries up to 1200 payload bytes plus a 16-byte fragment header. The host
  counts packets, bytes, message completeness and missing fragments. Board records
  send errors, queue drops and sender thread CPU time. No artificial packet loss
  or disconnect test is included in this first cost measurement.

The little-endian message consists of timestamp (uint64), position/quaternion
(7 doubles), width/height (2 uint32), and optional packed 8-bit grayscale image.
The fragment header is four uint32 values: magic, message ID, fragment ID, count.
This transport is an experimental framing format, not a stable product API.

The preliminary `results-20260918T004014` used synchronous per-frame SD reads.
Only baseline/pose completed; the next run was deliberately stopped. Those data
are excluded from the final CPU-contention comparison because I/O waits mask
sender contention. It used the earlier, unstripped experimental binary.

`results-20260918T004333` is another excluded preliminary series: its sender
polled every 1 ms, and baseline omitted sender buffers/thread initialization.
Pose hashes differed between modes, motivating equalized initialization and
semaphore-based blocking before the final comparison. Incomplete runs in that
series were deliberately stopped; they are not measurements.

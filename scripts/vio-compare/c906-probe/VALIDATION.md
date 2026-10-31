# c906-probe 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

## OpenVINS A53/C906 pipeline feasibility probe

This is a timing and communication check, plus a standalone scalar LK workload
on the C906. It does not replace OpenVINS's OpenCV tracker. It was run on
TideSense on 2026-09-25 against the 744-frame valid prefix of
`/mnt/data/vio-record-20260924-121320`, with headless OpenVINS and one OpenCV
thread. The temporary stage instrumentation and raw traces are under
`install/openvins-pipeline-20260925/`.

### A53 stage timing

The temporary build printed the existing `VioManager` timing fields after
each initialized camera update. Its 744 pose rows matched the uninstrumented
headless replay exactly apart from `compute_ms`. The 417 initialized frames
had these times:

| Stage | Mean | Median | P95 |
| --- | ---: | ---: | ---: |
| Stereo tracking | 40.5 ms | 38.6 ms | 47.9 ms |
| Propagation, updates, and marginalization | 34.5 ms | 25.4 ms | 88.2 ms |
| Total camera update | 75.1 ms | 64.3 ms | 136.3 ms |

The backend mean consists of 2.1 ms propagation, 5.0 ms MSCKF update,
11.8 ms SLAM update, 14.5 ms delayed SLAM initialization, and 1.1 ms
marginalization. These are per-frame means; some updates are much slower.

If a tracker with **exactly the A53 stage timings** could run independently
of the A53 backend, a two-stage schedule of the recorded work would take
17.17 s for 417 frames, or 24.3 frames/s, versus 31.30 s for the serial
camera work. This is a scheduling calculation, not a C906 measurement. A
fixed 20 Hz arrival simulation has up to 979 ms of transient pose delay even
at that ideal tracker speed. If the remote tracker takes 1.25 times the A53
tracking time, the simulated queue ends about 13 frames behind. Its mean
tracking time therefore needs to be well under 50 ms, including remote
execution and synchronization; the current A53 tracker mean is 40.5 ms.

The current replay calls tracking then propagation/update serially. A pipeline
would have to hand immutable feature observations for frame N to the backend
while the tracker works on frame N+1. Sharing the current mutable
`FeatureDatabase` directly across processors is not a safe interface.

### A53/C906 command roundtrip

`cmdqu_probe.c` is a temporary Linux module. It uses the SDK's exported
`rtos_cmdqu_send_wait()` and the FreeRTOS `SYS_CMD_INFO_LINUX` echo command;
no firmware change is required. On the running board, 100 matching replies
gave min 12 us, median 12 us, P95 15 us, max 83 us, and mean 14 us. This
measures only an 8-byte mailbox command and wakeup. Image buffers still need
shared memory plus explicit cache ownership and synchronization.

`kfree(wait_list)` after the helper already freed that allocation; this probe
calls the exported kernel helper directly.

### C906 sparse LK measurement

`lk_kernel.c` is a shared scalar C implementation built for both processors
with `-O2`. The A53 program copies two recorded 640x400 PGM images into a
1.2 MiB cached ION allocation, chooses 200 spaced high-gradient points, and
passes the physical address through `lk_cmd_module.ko`. The C906 invalidates
the input cache, builds five 2x2 downsampled image pyramid levels, tracks the
points with a 15x15 window and up to 30 iterations per level, and flushes
its result. The A53 then runs the same code and checks the output.

| Real recorded pair | C906 pyramid | C906 LK | C906 total | A53 scalar total | Point/iteration comparison |
| --- | ---: | ---: | ---: | ---: | --- |
| Left frames 401-402 | 11.36 ms | 217.56 ms | 228.92 ms | 147.7 ms | 200/200 valid; 3544 iterations; identical coordinates |
| Right frames 601-602 | 11.36 ms | 217.46 ms | 228.83 ms | 147.6 ms | 200/200 valid; 3528 iterations; identical coordinates |

Values are medians of five successive runs on each image pair. The command
roundtrip, including A53 result-cache invalidation, was about 0.10 ms more
than the measured C906 work. Input-cache flush and ION allocation are outside
that number. Raw output is in
`install/openvins-c906-lk-20260925/left-401-402.log` and
`right-601-602.log`.

This LK kernel uses central-difference gradients and 2x2 downsampling;
OpenCV uses Scharr derivatives, Gaussian pyramid filtering, fixed-point and
NEON optimizations on the A53. This is an implementation speed test, not a
claim that the two trackers have equal accuracy. The existing A53 OpenVINS
tracker averages 40.5 ms for *both* cameras and its other feature work. One
C906 scalar camera takes about 229 ms, so moving this implementation to the
small core does not meet a 50 ms stereo frame period. A future C906 vector
implementation would need a very large gain and must be measured separately.

The test firmware was made with `freertos-probe.patch` applied inside the
`freertos` subrepository. The patch also reduces the temporary FreeRTOS heap
from 512 to 504 KiB because the test code otherwise exceeds the reserved
768 KiB firmware region by about 2 KiB. The probe FIP was generated by
replacing only `BLCP_2ND` in the original FIP with SDK `fiptool.py`; BL2,
monitor, and U-Boot segments were verified unchanged. The original FIP was
restored on the device after testing and rechecked at SHA-256
`2ac832bd172d9dcb19972caa2eb420002ebf0a5374acc5e32f3a208459a326c5`.
The temporary kernel module was unloaded, the original FreeRTOS source was
restored, and the board booted and answered the command roundtrip probe.

### Decision

Cross-core communication and shared ION buffers work. The tested scalar C906
LK is about 1.55 times slower than the same A53 code and much slower than
the optimized A53 OpenCV tracker. It cannot sustain 20 Hz even for one
camera. Keep OpenVINS KLT on the A53 unless a substantially faster C906
tracker is demonstrated with the complete stereo workload.

### Other C906 offload candidates

The LK result does not rule out other work. On 2026-09-25, a separate A53
benchmark ran OpenCV's actual `buildOpticalFlowPyramid` at 640x400 for two
recorded images, 15x15 LK windows, and five requested pyramid levels. With
one OpenCV thread, 100 timed repetitions after warm-up averaged 10.77 ms
with derivatives (median 10.77 ms, P95 10.85 ms), versus 5.19 ms without
derivatives. The tracker requests derivatives by default. The current C906
probe's 11.36 ms *per-camera* simple 2x2 pyramid is not equivalent to this
OpenCV Gaussian pyramid and Scharr derivative output.

An additional 550-frame replay on the board enabled OpenVINS's existing
`[TIME-KLT]`, `[SLAM-DELAY]`, and `[SLAM-UP]` timers. Across its last 223
initialized frames, stereo pyramid plus derivatives averaged 11.06 ms,
feature top-off detection 3.31 ms, temporal LK 23.90 ms, and tracker feature
database update 1.82 ms. `UpdaterSLAM::delayed_init` was entered with
surviving features in 126 frames: triangulation averaged 1.95 ms per such
entry (about 1.10 ms per initialized frame); feature initialization averaged
21.92 ms per entry (about 12.39 ms per initialized frame). The latter calls
`StateHelper::initialize`, which modifies the current filter state and
covariance in feature order. Similarly, `[SLAM-UP]`'s linear-system creation
averaged 0.79 ms per call, versus 6.46 ms for the following state update.
Verbose logging can perturb the absolute timings; the independent pyramid
benchmark and previous 417-frame stage timing are the cross-checks.

The best first offload candidate is **next-frame stereo pyramid plus
derivatives**: the raw frame does not depend on the previous filter update,
and C906 could work while A53 finishes the current frame's backend. It must
produce OpenCV-compatible padded pyramid images and signed Scharr gradients,
and use shared DDR with explicit cache ownership. It is only profitable if
its *complete* C906 compute, copies, and synchronization fit under the A53
backend window, typically 34.5 ms on the earlier 417-frame replay. This
remains unmeasured on C906. The maximum A53 time saved is about 11 ms/frame.

The second candidate is previous-frame feature top-off detection. It can
begin after that frame's tracker updates its feature list, while A53 runs the
backend. Its measured A53 cost is about 3.3 ms/frame, so its maximum benefit
is small; exact FAST/grid selection and feature IDs must be preserved.
Per-feature triangulation in delayed initialization has only about 1 ms/frame
of measured A53 work to move. IMU propagation is about 2 ms/frame and depends
on the corrected filter state. Dense covariance updates dominate much of the
backend and cannot be pipelined ahead of the current state update without a
substantial estimator change.

Even granting zero-cost C906 execution and transfer for pyramid and
detection, the previous 75.1 ms/frame A53 mean would fall only to about
60.7 ms/frame, or roughly 16.5 frames/s. This is an upper bound on these
offloads, not an achieved result. Reaching 20 Hz requires more than 25 ms of
A53 work removed or overlapped per frame; CPU optimization of LK and backend
state updates remains necessary alongside any C906 preprocessor.

# TideSense StereoBM A53 benchmark

Independent stereo-depth benchmark: no VIO, IMU processing, ROS, IVE or TPU.
Uses the existing static OpenCV 3.4.20 libraries with the NEON CPU baseline,
GCC 11.3 `-O3 -mcpu=cortex-a53`, one OpenCV thread.

## Build and prepare

From the SDK root:

```sh
bash scripts/vio-compare/probes/stereo-bm/build.sh
/usr/bin/python3 -S - <<'PY'
import sys, runpy
sys.path.insert(0, '/usr/lib/python3/dist-packages')
sys.argv = ['prepare.py',
    'cali/tidesense-stereo-yy-equi-20261011-231002/stereo-camchain.yaml',
    'install/stereo-bm-a53']
runpy.run_path('scripts/vio-compare/probes/stereo-bm/prepare.py', run_name='__main__')
PY
```

The map generator uses the original raw-image Kalibr pinhole/equidistant
calibration, including the right camera's 180 degree installation. It computes
fisheye stereo rectification at 320x200 and 640x400 with `balance=0`. Right-image
rotation is folded into the fixed-point remap. Both raw inputs remain 640x400.
The calibrated baseline is 49.667 mm. Rectified focal length is 139.987 px at
320x200 or 279.974 px at 640x400.

For positive disparities this rig requires using the physical right rectified
view as the matcher reference. `rect0` and the depth images therefore refer to
the rectified physical right camera; `rect1` is the rectified physical left.
These labels do not change the dataset's left/right files.

Copy `stereo_bm`, `maps-*.bin`, `maps.json` and `run-board.sh` to a board directory.
Run on the board:

```sh
sh /mnt/data/stereo-bm-bench-20261012/run-board.sh \
  /mnt/data/stereo-bm-bench-20261012 \
  /mnt/data/openvins-imu-bench-20261012-084638/input \
  > /mnt/data/stereo-bm-bench-20261012/board-run.log 2>&1
```

Input is the existing lossless PGM export of `dataset-20261012-084638`, with
885 synchronized pairs. No frames are dropped or repeated. The shell runs three
configurations: 320x200/32 disparities, 640x400/64, 320x200/64, all with block 15.

## Timing and output

The timing CSV contains:

- `read_ms`: read and decode both original PGM images from SD.
- `rectify_ms`: remap both images, including distortion removal, right-image
  orientation, rectification and output resolution change.
- `bm_ms`: StereoBM, including its prefilter and built-in validation/filtering.
- `depth_ms`: convert valid disparities to 16-bit millimetres and count coverage.
- `process_ms`: rectification + StereoBM + depth conversion.
- `with_read_ms`: the above plus PGM reads, measured in a single frame interval.
- `valid_fraction`: valid depths / all output pixels, including image borders.

Maps and matcher are initialized once outside the frame timers. Timing CSV
writing and occasional diagnostic image writing are outside the frame timers.
Outputs are written to `/tmp` during each run and copied to SD after it finishes.
This is an offline compute benchmark; capture, transport and depth publishing
are not included. `1000 / mean(process_ms)` is processing capacity, not a
measured live camera output rate. SD replay rate is reported separately.

StereoBM settings: minimum disparity 0, XSobel prefilter, cap 31, block 15,
texture threshold 10, uniqueness ratio 15, speckle window 100/range 2,
`disp12MaxDiff=1`. This is a filtered baseline, not an unfiltered speed test.
Native disparities are signed 16-bit with 4 fractional bits. Depth conversion
uses the rectified focal length and calibrated baseline. Zero in the 16-bit
depth PNG means invalid; depth beyond 65.535 m is also marked invalid.

Samples at frames 150, 400 and 600 include rectified views, signed disparity
binary, and depth PNG. High texture coverage is not guaranteed on this recording
because it includes blank walls, nearby furniture, occlusion and fast motion.
No ground-truth depth is available, so coverage does not establish accuracy.

Copy board `results/` into `install/stereo-bm-a53/results/`, then summarize:

```sh
/usr/bin/python3 -S - <<'PY'
import sys, runpy
sys.path.insert(0, '/usr/lib/python3/dist-packages')
sys.argv = ['analyze.py', 'install/stereo-bm-a53']
runpy.run_path('scripts/vio-compare/probes/stereo-bm/analyze.py', run_name='__main__')
PY
```

The analysis excludes the first 20 warmup frames and writes `results.json`.
The experiment report and all board artifacts are in `install/stereo-bm-a53`.

## Hardware perf sampling

`build.sh` also retains `stereo_bm.sym`, the exact unstripped ELF from the same
link. Keep it with the stripped executable: these static binaries have no GNU
build ID. Building to a separate directory preserves the original benchmark.

```sh
bash scripts/vio-compare/probes/stereo-bm/build.sh \
  install/stereo-bm-a53/perf-20261013
```

Copy that executable and `profile-board.sh` into
`/mnt/data/stereo-bm-bench-20261012/perf-20261013/`, then run on the board:

```sh
sh /mnt/data/stereo-bm-bench-20261012/perf-20261013/profile-board.sh \
  > /mnt/data/stereo-bm-bench-20261012/perf-20261013/board-profile.log 2>&1
```

This records user-only `cycles` and `L1-dcache-load-misses` at 499 Hz per event,
with the monotonic clock. It runs 885 frames at 320x200/D32 and 640x400/D64.
`STEREO_BM_INTERVALS` enables a separate CSV with the read/rectify/BM/depth stage
boundaries; the serialization is outside frame timers. The algorithm and all
matcher settings remain identical. Traces and diagnostic outputs go to tmpfs
during sampling and are copied to SD afterward.

On the host, create a symfs entry for the exact board executable path pointing
to the unstripped ELF. Use a working host perf, e.g.
`/usr/lib/linux-tools/5.15.0-198-generic/perf`, to decode:

```sh
perf script --ns -F time,period,event,ip,sym,dso \
  -i 320x200-32-15-perf.data --symfs SYMFS > samples.txt
python3 scripts/vio-compare/probes/stereo-bm/analyze-perf.py \
  samples.txt 320x200-32-15-intervals.csv summary.json
```

`analyze-perf.py` excludes the first 20 frames, separates stage intervals and
weights samples by their periods. It reports self costs, not inclusive costs.
Check decoder warnings, unresolved symbols and LOST events before interpreting
the report. For assembly annotation, use `perf annotate --stdio --no-source`
with `--objdump` pointing to the AArch64 cross objdump.

The 2026-10-13 findings, raw traces, interval CSVs and instruction annotations
are in `install/stereo-bm-a53/perf-20261013/README.md`. Hardware sampling is not
precise instruction tracing: an event can be attributed to a nearby instruction.
Cache-refill event percentages do not measure the percentage of stall time.

## Recorded results

- [Baseline measurements](results/baseline-20261012.md), with [statistics](results/baseline-20261012.json).
- [Hardware perf findings](results/perf-20261013.md), with [320x200 rankings](results/perf-320x200-20261013.json), [640x400 rankings](results/perf-640x400-20261013.json) and [sampling provenance](results/perf-run-info-20261013.json).

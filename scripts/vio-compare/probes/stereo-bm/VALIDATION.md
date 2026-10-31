# stereo-bm 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

### Timing and output

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

### Hardware perf sampling

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

### Independent difference-update experiments

Build all opt-in variants without changing the SDK's installed OpenCV libraries:

```sh
python3 scripts/vio-compare/probes/stereo-bm/build-opt.py
```

`prepare-opt.py` generates an isolated translation unit from the SDK's OpenCV
3.4.20 source. It adds three independent macros, all disabled by default:

| Macro | Values | Effect |
|---|---|---|
| `CV_STEREOBM_A53_STRIPE_ROWS` | 0 / 32 / 64 | Explicit serial stripes with one reusable scratch buffer; 0 keeps original scheduling. |
| `CV_STEREOBM_A53_WIDE` | 0 / 1 | AArch64 NEON widening add/sub for the bounded short-SAD difference update. |
| `CV_STEREOBM_A53_RECOMPUTE` | 0 / 1 | Recompute the outgoing difference from unchanged prefiltered pixels; omit the difference ring on the SIMD short-SAD path. |

The scalar path keeps its original difference buffer. Source image boundary
clamping, disparity validation, subpixel interpolation and whole-image speckle
filtering are retained. Stripes explicitly invoke the matcher per strip;
changing only `nstripes` would not split work when OpenCV uses one thread.
The stripe parameter is a maximum height: the existing invoker distributes
rows evenly. For a limit of 64, 320x200 uses 50-row strips and 640x400 uses
57/58-row strips; for a limit of 32, the strips are 28/29 and 30/31 rows.

`build-opt.py` reuses the installed OpenCV compiler flags to compile only
`stereobm.cpp`, then links this object before the existing static libraries.
Each variant retains its exact unstripped ELF, compile command and binary hash.
The macro-off SIMD function's machine code was verified byte-identical to the
installed original object.

`run-opt-board.sh` compares `base`, `stripe32`, `stripe64`, `wide`, `recompute`,
`wide-recompute`, `all32`, and `all64`, with a baseline repeat at the end.
The screen uses 60 image pairs distributed across the full 885-pair recording,
preloaded in memory and processed three times. Timings exclude hashing and
output; the first 20 calls are excluded in analysis. `STEREO_BM_HASHES` records
a 64-bit FNV-1a fingerprint of every byte of each original signed disparity
image, including invalid pixels and subpixel bits. `STEREO_BM_PRELOAD` and
`STEREO_BM_REPEAT` are optional benchmark controls, not matching optimizations.

The exact screening selection is preserved in
[sample-stereo-20261013.csv](results/sample-stereo-20261013.csv): frames 0, 15,
30, ... 870 and 884 of the exported recording. Set up the board experiment
directory with:

- `bin/VARIANT`: copy each `VARIANT/stereo_bm` from the optimization build.
- `maps-320x200.bin` and `maps-640x400.bin`: copy from the baseline preparation.
- `input/stereo.csv`: copy the saved screening CSV.
- `input/left` and `input/right`: symlink to the corresponding directories in
  `/mnt/data/openvins-imu-bench-20261012-084638/input`.
- `run-opt-board.sh` and `run-opt-full.sh`: copy from this source directory.

Run `sh /mnt/data/stereo-bm-opt-20261013/run-opt-board.sh` to repeat the screen.

Copy the board results and summarize with:

```sh
/usr/bin/python3 -S - <<'PY'
import sys, runpy
sys.path.insert(0, '/usr/lib/python3/dist-packages')
sys.argv = ['analyze-opt.py', 'install/stereo-bm-a53/opt-20261013']
runpy.run_path('scripts/vio-compare/probes/stereo-bm/analyze-opt.py', run_name='__main__')
PY
```

The experiment report records screening and full-record verification separately
in `install/stereo-bm-a53/opt-20261013/README.md`.
The full verification command takes a size and a baseline-first list of variants:

```sh
sh /mnt/data/stereo-bm-opt-20261013/run-opt-full.sh \
  /mnt/data/stereo-bm-opt-20261013 320x200 base recompute
sh /mnt/data/stereo-bm-opt-20261013/run-opt-full.sh \
  /mnt/data/stereo-bm-opt-20261013 640x400 base all64
```

These runs stream all 885 original pairs rather than preloading them. Compare
the full-record results separately:

```sh
python3 scripts/vio-compare/probes/stereo-bm/analyze-opt.py \
  install/stereo-bm-a53/opt-20261013 \
  --results-subdir full-results --output full-results.json
```

Use the system NumPy environment shown above if the host Python environment
has incompatible packages.

### Recorded results

- [Baseline measurements](results/baseline-20261012.md), with
  [statistics](results/baseline-20261012.json).
- [Hardware perf findings](results/perf-20261013.md), with
  [320x200 rankings](results/perf-320x200-20261013.json),
  [640x400 rankings](results/perf-640x400-20261013.json) and
  [sampling provenance](results/perf-run-info-20261013.json).
- [Independent optimizations and full-record verification](results/optimization-20261013.md),
  with [screening statistics](results/optimization-screen-20261013.json),
  [full-record statistics](results/optimization-full-20261013.json) and
  [build/verification provenance](results/optimization-run-info-20261013.json).

These summaries are kept in Git. Raw recordings, traces, images, timing CSVs,
maps and executables remain in the artifact directories listed in each report.

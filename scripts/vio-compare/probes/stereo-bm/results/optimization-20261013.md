# StereoBM A53 difference-update experiments

2026-10-13, TideSense SG2002 A53 **1 GHz**, single-thread OpenCV 3.4.20 NEON, GCC 11.3 static. Recording: `dataset-20261012-084638`. No VIO, ROS, IVE or TPU. Matcher settings and fisheye calibration remain identical to the original depth benchmark.

## Full-record verification

Each run processes all **885 pairs**, reads original 640x400 PGM images from SD, and excludes the first 20 pairs in timing analysis. The table includes both-image rectification, BM and millimetre-depth conversion; file reads, hash computation and outputs are excluded.

| Output / configuration | BM ms | Compute total ms | Compute p95 ms | Compute capacity FPS | BM reduction | Total reduction |
|---|---:|---:|---:|---:|---:|---:|
| 320x200 / base | 26.53 | 35.53 | 39.39 | 28.14 | 0.00% | 0.00% |
| 320x200 / recompute | 23.19 | 32.12 | 36.10 | 31.14 | 12.59% | 9.61% |
| 640x400 / base | 221.97 | 252.45 | 266.50 | 3.96 | 0.00% | 0.00% |
| 640x400 / all64 | 137.25 | 167.76 | 181.68 | 5.96 | 38.17% | 33.55% |

All **885 per-frame raw-disparity fingerprints match** the baseline in each optimized full replay. At frames 150, 400 and 600, complete disparity files, rectified views and depth PNG files were also compared byte-for-byte and match. Hashes include invalid pixels and all subpixel bits. Coverage is unchanged. There is no depth ground truth, so these checks establish output preservation on this dataset, not absolute depth accuracy.

320x200 uses only outgoing-difference recomputation for the full verification: combinations add less than 1% of baseline BM time in the screen, so this retains the simpler implementation. At 640x400 the all64 combination gives the largest screen benefit. The capacity above is offline compute throughput; live capture and depth publication have not been measured. Full-resolution processing remains about 6 FPS.

## Independent screening results

60 pairs distributed across the full recording, held in RAM and processed three times: 180 calls/run, first 20 excluded. Each size includes a baseline repeat at the end. The baseline below is the mean of those two baseline runs. All 18 runs (3240 calls) have identical raw-disparity fingerprints.

| Variant | 320x200 BM ms | BM reduction | 640x400 BM ms | BM reduction |
|---|---:|---:|---:|---:|
| base | 26.19 | -0.08% | 223.61 | -0.51% |
| stripe32 | 26.88 | -2.72% | 164.85 | 25.90% |
| stripe64 | 24.72 | 5.54% | 150.19 | 32.49% |
| wide | 25.52 | 2.49% | 215.34 | 3.21% |
| recompute | 23.50 | 10.21% | 179.20 | 19.45% |
| wide-recompute | 23.55 | 10.00% | 174.71 | 21.47% |
| all32 | 25.08 | 4.17% | 149.03 | 33.01% |
| all64 | 23.32 | 10.90% | 137.70 | 38.11% |
| base-repeat | 26.15 | 0.08% | 221.34 | 0.51% |

The 32-row limit slows 320x200, while both stripe sizes help 640x400. Widening arithmetic is a small standalone benefit. Removing the difference ring helps both sizes. Combining gains is not additive. The 320x200 base and repeat differ by only 0.15% in BM time; 640x400 differs by about 1.0%. Sub-percent differences between combinations should not be treated as firm wins.

## Macros and implementation

| Macro | Default | Effect |
|---|---:|---|
| `CV_STEREOBM_A53_STRIPE_ROWS` | 0 | Explicit serial stripes; reuse one scratch buffer. Experiments set 32 or 64. |
| `CV_STEREOBM_A53_WIDE` | 0 | Use AArch64 UADDW/UADDW2 and USUBW/USUBW2 instead of separate widening plus saturated horizontal-sum arithmetic. |
| `CV_STEREOBM_A53_RECOMPUTE` | 0 | Recompute the outgoing byte difference from unchanged prefiltered images; omit SIMD short-SAD difference-ring allocation and writes. |

The widening path is selected only inside the original short-SAD path (cap <=31, window <=21). Horizontal sums including the entering column are bounded by 62*22, so widened 16-bit arithmetic cannot overflow. Scalar fallback retains the original difference ring. Boundary clamping, subpixel interpolation, validation and full-image speckle filtering are retained.

The stripe macro is an upper bound: rows are divided evenly by the original invoker. The 64 setting yields 50-row strips at 320x200 and 57/58-row strips at 640x400; the 32 setting yields 28/29 and 30/31 rows. Stripes are explicitly invoked because single-thread OpenCV otherwise passes the complete range to one matcher invocation.

`all64` = stripe limit 64 + wide + recompute; `all32` uses limit 32. `wide-recompute` enables the latter two only. The macro-off SIMD kernel was extracted from both object files and verified byte-identical to the installed original kernel.

## Reproduction and artifacts

- Build/reproduction documentation: `scripts/vio-compare/probes/stereo-bm/README.md`.
- `stereobm.cpp`, `stereobm-opt.patch`: generated opt-in source and reviewable patch.
- `variants.json`, `RUN-INFO.json`: compile commands, flags, source/binary hashes and checks.
- `results.json`, `results/`: screening statistics, timings and per-frame fingerprints.
- `full-results.json`, `full-results/`: full-record statistics, timings, fingerprints and sample disparity/depth outputs.
- `board-run.log`, `full-board-run.log`: clock, temperature and completion logs.
- `base/`, `stripe32/`, `stripe64/`, `wide/`, `recompute/`, `wide-recompute/`, `all32/`, `all64/`: isolated objects and stripped/unstripped executables.

Board directory: `/mnt/data/stereo-bm-opt-20261013/`. All experiment jobs have finished. All macros are off unless explicitly enabled in the experiment build.

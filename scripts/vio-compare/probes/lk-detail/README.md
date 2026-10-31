# OpenVINS LK and RANSAC A53 probe

This is an opt-in experiment for the TideSense A53 replay build. The
`openvins-core.patch` and `run_record.patch` contain the earlier IMU-guided
LK prediction probe plus separate timers for OpenCV LK, point undistortion,
fundamental-matrix RANSAC, and new-feature stereo LK. They add these
environment options:

| Option | Default | Effect |
| --- | ---: | --- |
| `OPENVINS_IMU_LK` | absent | Use integrated gyro rotation as temporal LK initial flow when present |
| `OPENVINS_LK_MAX_ITERS` | 30 | LK iteration cap, 1–30 |
| `OPENVINS_LK_MAX_LEVEL` | 5 | Pyramid level request, 0–5 |
| `OPENVINS_RANSAC_MAX_ITERS` | 1000 | Fundamental-matrix RANSAC cap, 16–1000 |
| `OPENVINS_LK_DETAIL` | absent | Emit `LK_DETAIL` timing and point counts |
| `OPENVINS_LK_PROBE` | absent | Emit existing whole-matching and good-track lines |

The optional `opencv-lk-iterations.patch` temporarily adds per-level
iteration histograms to OpenCV 3.4.20. Set `OPENVINS_LK_ITER_PROBE=1` on that
build to emit `LK_ITER`. Use a separate binary for this probe: its per-point
atomic counters and logging perturb timing. The standard speed comparisons
below use the uninstrumented OpenCV library.

Apply the OpenVINS patches to the clean checkout created by
`scripts/vio-compare/sync.sh`:

```sh
cd install/vio-compare-20260924-121320/openvins/open_vins
git apply ../../../../scripts/vio-compare/probes/lk-detail/openvins-core.patch
cd ..
patch -p1 < ../../../scripts/vio-compare/probes/lk-detail/run_record.patch
cmake --build build-a53 --target run_record -j4
```

`run_a53.sh` replays the first 800 stereo frames with five settings on the
board. The script assumes `run_record.lkdetail`, the exported PGM replay, and
the experiment config are already in `/mnt/data/vio-compare/openvins`.
`analyze.py` summarizes the downloaded logs and CSV trajectories. Raw results
are in the ignored local directory `install/openvins-pipeline-20260925/`.

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

# Basic transport cost on TideSense

This is an isolated experiment, not the production ROS bridge. It does not edit
the native entry point, SchurVINS source, existing build tree, or SD image.

## Build and run

Prerequisite: the existing GCC 11 `opt-all` build and its dependencies, with all
six NEON options enabled. `build.py` copies the native entry point into its own
ignored build directory and links the existing static libraries read-only.

```sh
python3 scripts/schurvins/bench/send-test/build.py
# Configure SSHPASS in the environment for the test board.
python3 scripts/schurvins/bench/send-test/run.py
python3 scripts/schurvins/bench/send-test/summarize.py <results-directory>
```

The runner uses board 192.168.123.99 and receiver 192.168.123.2:39281 on RNDIS.
It creates a uniquely named directory in board tmpfs and removes its own files
after successful completion. Existing calibration, image CSV and IMU files in
`/mnt/data/schur-opt-test` and the EuRoC replay directory are read-only.

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。

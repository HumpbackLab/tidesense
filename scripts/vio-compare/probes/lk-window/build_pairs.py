#!/usr/bin/env python3
"""Link a fixed-input LK benchmark against the same A53 OpenCV libraries."""
from pathlib import Path
import shlex
import subprocess

here = Path(__file__).resolve().parent
sdk = here.parents[3]
build = sdk / "install/openvins-backend-f32/a53-fp32/build"
work = sdk / "install/openvins-lk-window"
flags = []
for line in (build / "CMakeFiles/run_fisheye.dir/flags.make").read_text().splitlines():
    if line.startswith("CXX_"):
        flags += shlex.split(line.split(" = ", 1)[1])
cmd = shlex.split((build / "CMakeFiles/run_fisheye.dir/link.txt").read_text())
obj = work / "lk_pairs.o"
subprocess.run([cmd[0], *flags, "-g0", "-c", str(here / "lk_pairs.cpp"), "-o", str(obj)], check=True)
adapter = [arg for arg in cmd if arg.endswith("run_fisheye.cpp.o")]
assert len(adapter) == 1
cmd[cmd.index(adapter[0])] = str(obj)
binary = work / "lk_pairs"
cmd[cmd.index("-o") + 1] = str(binary)
subprocess.run(cmd, cwd=build, check=True)
subprocess.run([cmd[0].removesuffix("g++") + "strip", "--strip-debug", str(binary)], check=True)
print(binary)

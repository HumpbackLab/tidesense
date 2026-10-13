#!/usr/bin/env python3
"""Build independent opt-in StereoBM variants against the existing SDK stage."""
import argparse
import concurrent.futures
import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

here = Path(__file__).resolve().parent
sdk = here.parents[3]
p = argparse.ArgumentParser()
p.add_argument("--output", type=Path, default=sdk / "install/stereo-bm-a53/opt-20261013")
a = p.parse_args()
a.output = a.output.resolve()
src = sdk / "install/schurvins-build/deps/opencv-3.4.20/modules/calib3d/src/stereobm.cpp"
subprocess.run(["python3", str(here / "prepare-opt.py"), str(src), str(a.output / "stereobm.cpp")], check=True)
flags = sdk / "install/schurvins-gcc11-static/opencv/modules/calib3d/CMakeFiles/opencv_calib3d.dir/flags.make"
opts = {}
for line in flags.read_text().splitlines():
    if line.startswith("CXX_") and " = " in line:
        k, v = line.split(" = ", 1)
        opts[k] = shlex.split(v)
cross = sdk / "host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++"
variants = {"base": (0, 0, 0), "stripe32": (32, 0, 0), "stripe64": (64, 0, 0),
            "wide": (0, 1, 0), "recompute": (0, 0, 1), "wide-recompute": (0, 1, 1),
            "all32": (32, 1, 1), "all64": (64, 1, 1)}

def build(item):
    name, values = item
    out = a.output / name
    out.mkdir(parents=True, exist_ok=True)
    macros = dict(zip(["CV_STEREOBM_A53_STRIPE_ROWS", "CV_STEREOBM_A53_WIDE", "CV_STEREOBM_A53_RECOMPUTE"], values))
    cmd = [str(cross), *opts["CXX_DEFINES"], *opts["CXX_INCLUDES"], *opts["CXX_FLAGS"],
           "-I" + str(src.parent), *[f"-D{k}={v}" for k, v in macros.items()],
           "-c", str(a.output / "stereobm.cpp"), "-o", str(out / "stereobm.o")]
    with (out / "build.log").open("w") as log:
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
        env = dict(os.environ, STEREO_BM_OVERRIDE_OBJECT=str(out / "stereobm.o"))
        subprocess.run(["bash", str(here / "build.sh"), str(out)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    print("BUILT", name, flush=True)
    return name, {"macros": macros, "binary_sha256": hashlib.sha256((out / "stereo_bm").read_bytes()).hexdigest(),
                  "compile_command": cmd}

with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    built = dict(pool.map(build, variants.items()))
(a.output / "variants.json").write_text(json.dumps(built, indent=2) + "\n")

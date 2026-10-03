#!/usr/bin/env python3
"""Build isolated A53 LK window variants from the existing FP32 benchmark."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--precision", choices=("fp32", "mixed"), default="mixed")
args = parser.parse_args()
sdk = Path(__file__).resolve().parents[4]
base = sdk / f"install/openvins-backend-f32/a53-{args.precision}"
build = base / "build"
work = sdk / "install/openvins-lk-window" / args.precision
source = work / "source"
source.mkdir(parents=True, exist_ok=True)
source_dir = "fp32_source" if args.precision == "fp32" else "base_source"
track = base / source_dir / "ov_core/src/track"
shutil.copy2(track / "TrackKLT.h", source / "TrackKLT.h")
cpp = (track / "TrackKLT.cpp").read_text()
old = "void TrackKLT::feed_new_camera(const CameraData &message) {"
assert cpp.count(old) == 1
# The inline constructor is compiled into VioManager.cpp too. Set the window
# before preprocessing instead of changing the header's default initializer.
cpp = cpp.replace(old, old + """
#ifdef OPENVINS_LK_WINDOW_EXPERIMENT
  win_size = cv::Size(OPENVINS_LK_WINDOW_WIDTH, OPENVINS_LK_WINDOW_HEIGHT);
  static bool reported = false;
  if (!reported) {
    std::fprintf(stderr, "LK_WINDOW=%dx%d\\n", win_size.width, win_size.height);
    reported = true;
  }
#endif
""")
(source / "TrackKLT.cpp").write_text(cpp)
flags = {}
for line in (build / "CMakeFiles/ov_msckf_lib.dir/flags.make").read_text().splitlines():
    if line.startswith("CXX_"):
        key, value = line.split(" = ", 1)
        flags[key] = shlex.split(value)
link = shlex.split((build / "CMakeFiles/run_fisheye.dir/link.txt").read_text())
compiler = link[0]
ar = compiler.removesuffix("g++") + "ar"
strip = compiler.removesuffix("g++") + "strip"
library = build / "libov_msckf_lib.a"
members = subprocess.check_output([ar, "t", str(library)], text=True).splitlines()
assert members.count("TrackKLT.cpp.o") == 1
commands = []
def run(cmd, **kwargs):
    commands.append(cmd)
    subprocess.run(cmd, check=True, **kwargs)

for width, height in ((15, 15), (16, 15), (16, 16)):
    out = work / f"{width}x{height}"
    out.mkdir(exist_ok=True)
    obj = out / "TrackKLT.cpp.o"
    run([compiler, *flags["CXX_DEFINES"], *flags["CXX_INCLUDES"],
         *flags["CXX_FLAGS"], "-g0", f"-I{track}",
         "-DOPENVINS_LK_WINDOW_EXPERIMENT=1",
         f"-DOPENVINS_LK_WINDOW_WIDTH={width}", f"-DOPENVINS_LK_WINDOW_HEIGHT={height}",
         "-c", str(source / "TrackKLT.cpp"), "-o", str(obj)])
    lib = out / library.name
    shutil.copy2(library, lib)
    run([ar, "r", str(lib), str(obj)])
    output = work / f"run_fisheye.{width}x{height}"
    cmd = [str(lib) if arg == library.name else arg for arg in link]
    cmd[cmd.index("-o") + 1] = str(output)
    run(cmd, cwd=build)
    run([strip, "--strip-debug", str(output)])
    print(output, flush=True)
(work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")

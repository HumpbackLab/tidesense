#!/usr/bin/env python3
"""Build isolated A53 warp tail variants from an existing GCC11 micro-ROS core.
Run the micro-ROS build with existing NEON options and benchmarks enabled first.
Does not install or replace the production executable. New options are controlled
per object, so the existing core's tail-option value does not affect the baseline.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
sdk = Path(__file__).resolve().parents[3]
core = sdk / 'install/schurvins-gcc11-static/core-micro-ros'
cache = (core / 'CMakeCache.txt').read_text()
for opt in ['HALFSAMPLE', 'ZMSSD', 'FAST10', 'ALIGN2D', 'ALIGN2D_HESSIAN', 'WARP_AFFINE']:
    if f'SCHUR_OPT_{opt}_NEON:BOOL=ON' not in cache:
        raise SystemExit(f'Baseline requires SCHUR_OPT_{opt}_NEON=ON')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
variants = {'base': [], 'tail': ['WARP_TAIL2']}

def flags(target):
    result = []
    for line in (core / f'CMakeFiles/{target}.dir/flags.make').read_text().splitlines():
        if line.startswith(('CXX_DEFINES =', 'CXX_INCLUDES =', 'CXX_FLAGS =')):
            result += shlex.split(line.split('=', 1)[1])
    return result

links = {t: shlex.split((core / f'CMakeFiles/{t}.dir/link.txt').read_text())
         for t in ['schurvins_stereo', 'schurvins_warp_bench']}
compiler = links['schurvins_stereo'][0]
ar = str(Path(compiler).with_name('aarch64-none-linux-gnu-ar'))
strip = str(Path(compiler).with_name('aarch64-none-linux-gnu-strip'))
objects = {}
for name, defs in variants.items():
    folder = out / name
    folder.mkdir(exist_ok=True)
    lib = folder / 'libschurvins.a'
    shutil.copy2(core / 'libschurvins.a', lib)
    for filename, base_defs in [
        ('patch_warp', ['WARP_AFFINE_NEON'])]:
        relevant = tuple(defs)
        key = (filename, relevant)
        obj = folder / (filename + '.cpp.o')
        if key not in objects:
            src = sdk / f'SchurVINS/svo_direct/src/{filename}.cpp'
            subprocess.run([compiler, *flags('schurvins'),
                            *['-DSCHUR_OPT_' + d + '=1' for d in base_defs + list(relevant)],
                            '-c', str(src), '-o', str(obj)], check=True)
            objects[key] = obj
        subprocess.run([ar, 'r', str(lib), str(objects[key])], check=True)
    for target, command in links.items():
        link = list(command)
        link[link.index('libschurvins.a')] = str(lib)
        exe = folder / target
        link[link.index('-o') + 1] = str(exe)
        subprocess.run(link, cwd=core, check=True)
        subprocess.run([strip, '--strip-unneeded', str(exe)], check=True)
    (folder / 'options.txt').write_text('\n'.join(defs) + '\n')
    print(name, 'ready', flush=True)

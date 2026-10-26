#!/usr/bin/env python3
"""Bundle the target ROS libraries and matching compiler runtime privately."""
import argparse
from pathlib import Path
import shutil
import re
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path)
p.add_argument('--binary',type=Path,action='append',default=[],help='Include all runtime dependencies of this ELF')
a=p.parse_args()
sdk=Path(__file__).resolve().parents[3]
prefix=sdk/'install/native-dds/ws/install'
cross=sdk/'host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-'
a.output.mkdir(parents=True,exist_ok=True)
for source in (prefix/'lib').rglob('*.so*'):
    if source.is_file():
        shutil.copy2(source.resolve(),a.output/source.name)
for name in ('libstdc++.so.6','libgcc_s.so.1','libc.so.6','libm.so.6','libpthread.so.0','libdl.so.2','librt.so.1','ld-linux-aarch64.so.1'):
    source=Path(subprocess.check_output([str(cross)+'g++','-print-file-name='+name],text=True).strip())
    if not source.is_file():
        raise SystemExit('Missing compiler runtime: '+str(source))
    shutil.copy2(source.resolve(),a.output/name)
# Resolve every DT_NEEDED from target files or the SDK compiler, never host libraries.
pending=list(a.output.iterdir())+a.binary
seen=set()
while pending:
    binary=pending.pop()
    if binary in seen or not binary.is_file():
        continue
    seen.add(binary)
    dynamic=subprocess.check_output([str(cross)+'readelf','-dW',str(binary)],text=True)
    for name in re.findall(r'Shared library: \[(.*?)\]',dynamic):
        target=a.output/name
        if target.is_file():
            continue
        source=prefix/'lib'/name
        if not source.is_file():
            source=Path(subprocess.check_output([str(cross)+'g++','-print-file-name='+name],text=True).strip())
        if not source.is_file():
            raise SystemExit(f'Missing target runtime {name} required by {binary}')
        shutil.copy2(source.resolve(),target)
        pending.append(target)
for binary in a.output.iterdir():
    if binary.is_file():
        subprocess.run([str(cross)+'strip','--strip-unneeded',str(binary)],check=True)

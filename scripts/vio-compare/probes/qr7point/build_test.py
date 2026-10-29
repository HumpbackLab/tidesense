#!/usr/bin/env python3
"""Build QR7 regressions with exactly the OpenCV dependencies used by replay."""
import argparse,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arch',choices=['host','a53'],default='host')
p.add_argument('--sanitize',action='store_true')
a=p.parse_args()
if a.sanitize and a.arch!='host':p.error('Sanitizers require --arch host')
root=Path(__file__).resolve().parents[4]
build=root/('install/openvins-c906-tpu/build-'+a.arch)
if '#define OPENVINS_FRONT_QR7POINT 1' not in (build/'openvins_frontend_config.h').read_text():
    raise RuntimeError('Build the core with --frontend --qr7 first')
link=shlex.split((build/'CMakeFiles/run_record.dir/link.txt').read_text())
libs=[v for v in link[link.index('-o')+2:] if not Path(v).name.startswith('libov_')]
stage=root/('install/schurvins-gcc11-static/stage' if a.arch=='a53' else 'install/dataset-analysis-20260924-155813/pc/stage')
out=root/'install/openvins-c906-frontend/qr7';out.mkdir(exist_ok=True)
binary=out/('qr7_test-'+a.arch+('-asan' if a.sanitize else ''))
cmd=[link[0],'-O3','-std=c++14','-I/usr/include/eigen3','-I'+str(stage/'include'),'-I'+str(root/'scripts/vio-compare/openvins'),'-I'+str(build),str(root/'scripts/vio-compare/openvins/qr_fundamental.cpp'),str(Path(__file__).with_name('test.cpp')),'-o',str(binary)]
if a.arch=='a53':cmd+=['-static','-mcpu=cortex-a53']
if a.sanitize:cmd+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run([*cmd,*libs],cwd=build,check=True)
print(binary)

#!/usr/bin/env python3
"""Build the isolated window-gradient LK probe; estimator and firmware are unchanged."""
import argparse,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arch',choices=['host','a53'],default='a53')
p.add_argument('--sanitize',action='store_true');a=p.parse_args()
if a.sanitize and a.arch!='host':p.error('Sanitizers are host-only')
here=Path(__file__).resolve().parent;root=here.parents[3]
out=root/'install/openvins-c906-frontend/cpu-window-lk';out.mkdir(parents=True,exist_ok=True)
build=root/('install/openvins-c906-tpu/build-'+a.arch)
link=shlex.split((build/'CMakeFiles/run_record.dir/link.txt').read_text())
libs=[v for v in link[link.index('-o')+2:] if not Path(v).name.startswith('libov_')]
flags={}
for line in (build/'CMakeFiles/run_record.dir/flags.make').read_text().splitlines():
 if ' = ' in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
extra=[];compiler='g++-11';args=['-O3','-DNDEBUG','-std=c++14','-Wall','-Wextra',*flags['CXX_INCLUDES']]
if a.arch=='a53':
 cross=root/'host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-'
 compiler=str(cross)+'g++'
 args+=['-mcpu=cortex-a53','-DCPU_WINDOW_LK_DEVICE=1','-I'+str(root/'cvi_mpi/include')]
 job=out/'lk_jobs.o'
 subprocess.run([str(cross)+'gcc','-O3','-mcpu=cortex-a53','-DLK_TPU_ROI_CACHE=1','-DLK_TPU_DUAL_BATCH=1','-DLK_TPU_BATCH=16','-I'+str(root/'cvi_mpi/include'),'-I'+str(root/'cvikernel/include'),'-c',str(here.parent/'c906-tpu/lk_jobs.c'),'-o',str(job)],check=True)
 extra=[str(job),'-static']
if a.sanitize:args+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
binary=out/('window_lk-'+a.arch+('-asan' if a.sanitize else ''))
subprocess.run([compiler,*args,str(here/'bench.cpp'),*extra,*libs,'-o',str(binary)],cwd=build,check=True)
if a.arch=='a53':
 subprocess.run([str(cross)+'strip',str(binary)],check=True)
print(binary)

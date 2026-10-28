#!/usr/bin/env python3
"""Build isolated A53 LK comparison using the existing static OpenCV stage."""
from pathlib import Path
import os, shlex, subprocess
here = Path(__file__).resolve().parent
repo = here.parents[3]
out = Path(os.environ.get('C906_OUT', repo / 'install/c906-reload'))
out.mkdir(parents=True, exist_ok=True)
cross = str(repo/'host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-')
includes = ['-I'+str(repo/'cvi_mpi/include'), '-I'+str(repo/'cvikernel/include'),
            '-I'+str(repo/'install/schurvins-gcc11-static/stage/include'),
            '-isystem', '/usr/include/eigen3',
            '-DLK_TPU_BATCH='+os.environ.get('C906_LK_BATCH','16')]
subprocess.run([cross+'gcc','-O3','-mcpu=cortex-a53','-Wall',*includes,'-c',str(here/'lk_jobs.c'),'-o',str(out/'lk_jobs.o')],check=True)
subprocess.run([cross+'g++','-O3','-mcpu=cortex-a53','-std=c++14','-Wall',*includes,'-c',str(here/'lk_host.cpp'),'-o',str(out/'lk_host.o')],check=True)
subprocess.run([cross+'g++','-O3','-mcpu=cortex-a53','-std=c++14','-Wall',*includes,'-c',str(here/'lk_overlap.cpp'),'-o',str(out/'lk_overlap.o')],check=True)
# Only reuse the link dependencies, not the FP32 estimator flags or code.
build = repo/'install/openvins-backend-f32/a53-fp32/build'
link = shlex.split((build/'CMakeFiles/run_fisheye.dir/link.txt').read_text())
libs = link[link.index('-o')+2:]
libs = [lib for lib in libs if not Path(lib).name.startswith('libov_')]
subprocess.run([cross+'g++','-O3','-mcpu=cortex-a53','-static',str(out/'lk_host.o'),str(out/'lk_jobs.o'),str(out/'lk_overlap.o'),
               '-Wl,--start-group',*[str(repo/'cvi_mpi/lib'/lib) for lib in
                 ['libsys.a','libcvikernel-static.a','libcviruntime-static.a']],
               '-Wl,--end-group',*libs,'-o',str(out/'c906-tpu-lk')],cwd=build,check=True)
print(out/'c906-tpu-lk')

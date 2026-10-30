#!/usr/bin/env python3
from pathlib import Path
import os,shlex,subprocess,sys
here=Path(__file__).resolve().parent;sdk=here.parents[3];arch=sys.argv[1]if len(sys.argv)>1 else'host';build=sdk/('install/openvins-c906-tpu/build-'+arch);out=sdk/'install/openvins-c906-frontend';out.mkdir(exist_ok=True)
flags={}
for line in (build/'CMakeFiles/run_record.dir/flags.make').read_text().splitlines():
 if ' = 'in line:
  k,v=line.split(' = ',1);flags[k]=shlex.split(v)
args=flags['CXX_DEFINES']+flags['CXX_INCLUDES']+flags['CXX_FLAGS']+['-I'+str(sdk/'cvi_mpi/include')]
# Tests choose the layout explicitly, independent of the last application build.
args=[x for x in args if not x.startswith('-DFRONT_ALIGNED_IMAGES')]
if os.environ.get('FRONT_TEST_FAST_FILL')=='1':args+=['-DFRONT_FAST_FILL=1']
for option in os.environ.get('FRONT_TEST_OPTS','').split(','):
 if not option:continue
 if option not in ['SPARSE_NMS','LAZY_MASK','ALIGNED_IMAGES']:raise ValueError('Unknown frontend test option: '+option)
 args+=['-DFRONT_'+option+'=1']
compiler='g++-11'if arch=='host'else str(sdk/'host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++')
if arch=='a53':args+=['-DFRONT_DEVICE_TEST=1']
for name in ['front_kernel','front_test']:
 subprocess.run([compiler,*args,'-I'+str(here),'-c',str(here/(name+'.cpp')),'-o',str(out/(name+'.o'))],check=True)
link=shlex.split((build/'CMakeFiles/run_record.dir/link.txt').read_text());i=link.index('-o')
extra=[]
if arch=='a53':
 subprocess.run([compiler.removesuffix('g++')+'gcc',*[x for x in args if not x.startswith('-std=')],'-I'+str(sdk/'cvikernel/include'),'-c',str(here/'front_jobs.c'),'-o',str(out/'front_jobs.o')],check=True)
 extra=[str(out/'front_jobs.o'),str(build/'CMakeFiles/ov_msckf_lib.dir/home/ncer/sophpi/scripts/vio-compare/probes/c906-tpu/lk_jobs.c.o')]
subprocess.run([compiler,str(out/'front_kernel.o'),str(out/'front_test.o'),*extra,*(['-static']if arch=='a53'else[]),'-o',str(out/('front_test-'+arch)),*link[i+2:]],cwd=build,check=True)
print(out/('front_test-'+arch))

#!/usr/bin/env python3
"""Build the isolated A53 register generator and C906 IVE probe client."""
from pathlib import Path
import subprocess
here=Path(__file__).resolve().parent;root=here.parents[3]
out=root/'install/openvins-c906-frontend/ive-c906';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(here/'generate_sdk.py'),str(out/'ive_sdk_generated.inc')],check=True)
cc=root/'host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc'
subprocess.run([str(cc),'-O3','-std=gnu11','-mcpu=cortex-a53','-static','-I'+str(here),'-I'+str(root/'cvi_mpi/include'),'-I'+str(root/'osdrv/interdrv/ive/hal/cv181x'),'-I'+str(out),str(here/'host.c'),str(here/'ive_jobs.c'),str(root/'cvi_mpi/lib/libsys.a'),'-lpthread','-lm','-o',str(out/'ive-probe.a53')],check=True)
print(out/'ive-probe.a53')

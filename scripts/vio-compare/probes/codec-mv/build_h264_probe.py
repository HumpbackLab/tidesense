#!/usr/bin/env python3
"""Link an isolated H264 read-only snapshot probe from existing sample objects."""
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
BASE=ROOT/'install/openvins-codec-shallow/device'
OUT=ROOT/'install/venc-h264-direct-20261017'

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    link=json.loads((BASE/'build-command.json').read_text())
    compiler=link[0]
    wrapper=OUT/'h264_probe.o'
    subprocess.run([compiler.removesuffix('g++')+'gcc','-O2','-g','-D__CV181X__','-I'+str(ROOT/'cvi_mpi/include'),
                    '-c',str(HERE/'h264_probe.c'),'-o',str(wrapper)],check=True)
    link=[str(wrapper) if s==str(BASE/'device_probe.o') else s for s in link]
    assert str(wrapper) in link
    link[link.index('-o')+1]=str(OUT/'sample_h264_probe')
    subprocess.run(link,cwd=ROOT/'cvi_mpi/sample/venc',check=True)
    subprocess.run([compiler.removesuffix('g++')+'strip',str(OUT/'sample_h264_probe')],check=True)
    (OUT/'build-command.json').write_text(json.dumps(link,indent=2)+'\n')

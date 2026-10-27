#!/usr/bin/env python3
"""Relink an isolated RKIVE probe, optionally replacing selected archive members."""
from pathlib import Path
import argparse
import json
import shlex
import subprocess

scripts=Path(__file__).resolve().parent
repo=scripts.parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--base',type=Path,default=repo/'install/rv1103-openvins-20261025')
parser.add_argument('--output',type=Path,default=repo/'install/rv1103-rkive-20261025')
parser.add_argument('--replace-source',type=Path,action='append',default=[],
                    help='Compile a replacement archive member with the selected build flags')
parser.add_argument('--track-source',type=Path)
args=parser.parse_args()
base=args.base.resolve()
build=base/'build'
out=args.output.resolve()
out.mkdir(parents=True,exist_ok=True)
sdk=Path('/home/ncer/luckfox-vt')
ive=sdk/'media/ive/ive'
flags={}
for line in (build/'CMakeFiles/openvins_full_native.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        key,value=line.split(' = ',1)
        flags[key]=shlex.split(value)
common=flags['CXX_DEFINES']+flags['CXX_INCLUDES']+flags['CXX_FLAGS']+['-std=c++17','-I'+str(scripts),'-I'+str(base/'src/third_party/open_vins/ov_core/src/track'),'-I'+str(ive/'include'),'-I'+str(sdk/'media/out/include')]
track=(args.track_source or base/'src/third_party/open_vins/ov_core/src/track/TrackKLT.cpp').read_text()
track='#include "rkive-frontend.h"\n'+track
old='cv::buildOpticalFlowPyramid(img, imgpyr, win_size, pyr_levels);'
assert track.count(old)==1
track=track.replace(old,'rkive_frontend::pyramid(img, imgpyr, win_size, pyr_levels);')
old='cv::calcOpticalFlowPyrLK(img0pyr, img1pyr, pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit, cv::OPTFLOW_USE_INITIAL_FLOW);'
assert track.count(old)==1
track=track.replace(old,'rkive_frontend::flow(img0pyr, img1pyr, pts0, pts1, mask_klt, error, win_size, pyr_levels, term_crit, cv::OPTFLOW_USE_INITIAL_FLOW);')
(out/'TrackKLT.cpp').write_text(track)
commands=[]
def run(args,cwd=None):
    commands.append(args)
    print(shlex.join(args),flush=True)
    subprocess.run(args,cwd=cwd,check=True)
sources=[(out/'TrackKLT.cpp','TrackKLT'),(scripts/'rkive-frontend.cpp','rkive-frontend')]
sources += [(source.resolve(),source.stem) for source in args.replace_source]
names=[name for _,name in sources]+['rkive-memory']
assert len(set(names))==len(names), 'Duplicate replacement object name'
for source,name in sources:
    run(['arm-linux-gnueabihf-g++',*common,'-c',str(source),'-o',str(out/(name+'.o'))])
run(['arm-linux-gnueabihf-gcc','-O3','-mcpu=cortex-a7','-mfpu=neon-vfpv4','-mfloat-abi=hard','-c',str(scripts/'rkive-memory.c'),'-o',str(out/'rkive-memory.o')])
link=shlex.split((build/'CMakeFiles/openvins_replay_benchmark.dir/link.txt').read_text())
index=link.index('libopenvins_full_native.a')
link[index:index]=[str(out/(name+'.o')) for name in names]
link[link.index('-o')+1]=str(out/'openvins_mono_rkive')
link += [str(ive/'lib/librve.a'),'-lpthread','-ldl','-lm']
run(link,cwd=build)
(out/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print('Built',out/'openvins_mono_rkive')

#!/usr/bin/env python3
"""Build isolated FP64 OpenVINS with optional C906/TPU raw-flow lookahead."""
import argparse
from pathlib import Path
import shutil
import subprocess

here=Path(__file__).resolve().parent
sdk=here.parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--arch',choices=['a53','host'],default='a53')
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--frontend',action='store_true',help='Compile optional whole-pixel frontend; runtime remains opt-in')
p.add_argument('--early-pipeline',action='store_true',help='Compile early image preparation; requires --frontend')
a=p.parse_args()
if a.early_pipeline and not a.frontend:p.error('--early-pipeline requires --frontend')
work=sdk/'install/openvins-c906-tpu'
source=work/'source'
if not source.exists():
    source.mkdir(parents=True)
    upstream=sdk/'install/vio-compare-20260924-121320/openvins/open_vins'
    archive=subprocess.Popen(['git','-C',str(upstream),'archive','69488123ed9362dd44b6f28e7f4680abbff1442b'],stdout=subprocess.PIPE)
    subprocess.run(['tar','xf','-','-C',str(source)],stdin=archive.stdout,check=True)
    archive.stdout.close()
    if archive.wait():raise RuntimeError('OpenVINS source archive failed')
    for name in ['offline','headless','lk-window','lk-filter','ransac-cap']:
        subprocess.run(['git','apply','--unsafe-paths','--directory='+str(source),
                        str(sdk/f'scripts/vio-compare/patches/openvins-{name}.patch')],check=True)
patch=sdk/'scripts/vio-compare/patches/openvins-c906-tpu.patch'
if subprocess.run(['patch','-p1','-R','--dry-run','--batch','--force','-i',str(patch)],cwd=source,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
    subprocess.run(['patch','-p1','--forward','--batch','-i',str(patch)],cwd=source,check=True)
state=(source/'ov_msckf/src/state/State.h').read_text()
if 'Eigen::MatrixXd _Cov;' not in state:raise RuntimeError('FP64 covariance required')
# CMake defines the unused fisheye target too. Avoid rewriting shared sources
# when host and A53 builds run independently in their own build directories.
for name in ['run_record.cpp','run_fisheye.cpp']:
    target=work/name
    if not target.exists() or target.read_bytes()!=(here/'run_tpu_record.cpp').read_bytes():
        shutil.copy2(here/'run_tpu_record.cpp',target)
flags=['-DCMAKE_BUILD_TYPE=Release','-DENABLE_ROS=OFF','-DENABLE_ARUCO_TAGS=OFF',
       '-DOPENVINS_SKIP_ACTIVE_VIZ=ON','-DOPENVINS_LK_WINDOW_16X15=ON','-DOPENVINS_LK_FILTER_VALID=ON',
       '-DOPENVINS_RANSAC_CAP_200=OFF','-DOPENVINS_C906_TPU_LK=ON',f'-DOPENVINS_TPU_SDK_ROOT={sdk}',
       '-DEigen3_DIR=/usr/share/eigen3/cmake',
       '-DOPENVINS_C906_FRONTEND='+('ON'if a.frontend else'OFF'),
       '-DOPENVINS_C906_FRONTEND_EARLY='+('ON'if a.early_pipeline else'OFF')]
if a.arch=='a53':
    flags += [f'-DCMAKE_TOOLCHAIN_FILE={here}/toolchain.cmake',
              f'-DBOOST_ROOT={sdk}/install/vio-compare-20260924-121320/openvins/deps',
              f'-DOpenCV_DIR={sdk}/install/schurvins-gcc11-static/stage/share/OpenCV',
              f'-DCeres_DIR={sdk}/install/vio-compare-20260924-121320/deps/ceres/lib/cmake/Ceres']
else:
    flags += ['-DCMAKE_C_COMPILER=gcc-11','-DCMAKE_CXX_COMPILER=g++-11',
              f'-DOpenCV_DIR={sdk}/install/dataset-analysis-20260924-155813/pc/stage/share/OpenCV',
              '-DCeres_DIR=/home/ncer/fpga_vio/.tools/ceres-1.14/lib/cmake/Ceres']
build=work/('build-'+a.arch)
subprocess.run(['cmake','-S',str(source/'ov_msckf'),'-B',str(build),*flags],check=True)
compile_flags=(build/'CMakeFiles/ov_msckf_lib.dir/flags.make').read_text()
for forbidden in ['-DOPENVINS_FP32_BACKEND','-DOPENVINS_EKF_MA_EXPERIMENT','eigen_mixed.h']:
    if forbidden in compile_flags:raise RuntimeError('FP64 build contains '+forbidden)
subprocess.run(['cmake','--build',str(build),'--target','run_record','-j',str(a.jobs)],check=True)
print(build/'run_record')

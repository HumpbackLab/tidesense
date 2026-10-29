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
p.add_argument('--sparse-nms',action='store_true',help='Visit only FAST candidates for NMS; requires --frontend')
p.add_argument('--lazy-mask',action='store_true',help='Build detection masks only when topping up; requires --frontend')
p.add_argument('--lk-batch',type=int,choices=[8,16,32],default=16,help='Must match C906 firmware lane count')
p.add_argument('--latest-intrinsics',action='store_true',help='Publish latest coherent calibration to C906 without a frame barrier')
p.add_argument('--lk-roi-cache',action='store_true',help='Compile current-image LK window cache; requires --frontend')
p.add_argument('--lk-dual',action='store_true',help='Pair independent 16-lane LK batches with TDMA/TIU overlap; requires --lk-roi-cache')
p.add_argument('--qr7',action='store_true',help='Compile guarded double QR seven-point solver; requires --frontend')
p.add_argument('--cpu-tail',action='store_true',help='Compile optional A53 16-point LK tail helper; requires --frontend')
a=p.parse_args()
if a.qr7 and not a.frontend:p.error('--qr7 requires --frontend')
if a.lk_dual and (not a.lk_roi_cache or a.lk_batch!=16):p.error('--lk-dual requires --lk-roi-cache and --lk-batch 16')
if a.early_pipeline and not a.frontend:p.error('--early-pipeline requires --frontend')
if (a.sparse_nms or a.lazy_mask or a.latest_intrinsics or a.lk_roi_cache or a.cpu_tail) and not a.frontend:p.error('Frontend optimizations require --frontend')
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
       '-DOPENVINS_C906_FRONTEND_EARLY='+('ON'if a.early_pipeline else'OFF'),
       '-DOPENVINS_FRONT_SPARSE_NMS='+('ON'if a.sparse_nms else'OFF'),
       '-DOPENVINS_FRONT_LAZY_MASK='+('ON'if a.lazy_mask else'OFF'),
       '-DOPENVINS_FRONT_LATEST_INTRINSICS='+('ON'if a.latest_intrinsics else'OFF'),
       '-DOPENVINS_TPU_LK_BATCH='+str(a.lk_batch),
       '-DOPENVINS_TPU_LK_ROI_CACHE='+('ON'if a.lk_roi_cache else'OFF'),
       '-DOPENVINS_TPU_LK_DUAL_BATCH='+('ON'if a.lk_dual else'OFF'),
       '-DOPENVINS_FRONT_QR7POINT='+('ON'if a.qr7 else'OFF'),
       '-DOPENVINS_FRONT_CPU_TAIL='+('ON'if a.cpu_tail else'OFF')]
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

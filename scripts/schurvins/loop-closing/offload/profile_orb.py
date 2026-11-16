#!/usr/bin/env python3
"""Instrument the pinned OpenCV ORB source without changing its algorithm."""
from pathlib import Path
root=Path(__file__).resolve().parents[4]
source=root/'install/schurvins-build/deps/opencv-3.4.20/modules/features2d/src/orb.cpp'
out=root/'install/schur-loop-offload/orb-profile/orb.cpp'
text=source.read_text()
preamble=r'''
#include <chrono>
#include <iostream>
#include <time.h>
namespace orbprof {
struct Stats { unsigned calls=0,provided=0; double total=0,pyramid=0,keypoints=0,fast=0,harris=0,angles=0,blur=0,descriptor=0;
 unsigned long long level_calls[32]{},raw[32]{},border[32]{},retained[32]{},final[32]{}; };
static thread_local Stats stats;
struct Scope {double& sum;std::chrono::steady_clock::time_point begin;
 explicit Scope(double& s):sum(s),begin(std::chrono::steady_clock::now()){}
 ~Scope(){sum+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}};
}
extern "C" void tidesense_orb_profile_report() {
 auto& s=orbprof::stats;
 std::cout<<"ORB_PROFILE calls="<<s.calls<<" provided="<<s.provided<<" total_ms="<<s.total
 <<" pyramid_ms="<<s.pyramid<<" keypoints_ms="<<s.keypoints<<" detector_ms="<<s.fast
 <<" harris_ms="<<s.harris<<" angles_ms="<<s.angles<<" blur_ms="<<s.blur<<" descriptor_ms="<<s.descriptor<<'\n';
 for(unsigned level=0;level<32;++level)if(s.level_calls[level])
  std::cout<<"ORB_POINTS level="<<level<<" calls="<<s.level_calls[level]<<" raw="<<s.raw[level]
   <<" border="<<s.border[level]<<" retained="<<s.retained[level]<<" final="<<s.final[level]<<'\n';
}
'''
def once(old,new):
 global text
 assert text.count(old)==1,old
 text=text.replace(old,new)
def scope(signature,field,extra=''):
 global text
 start=text.index(signature);brace=text.index('{',start)
 text=text[:brace+1]+f'\n    orbprof::Scope profile_{field}(orbprof::stats.{field});{extra}\n'+text[brace+1:]
scope('\nHarrisResponses(', 'harris')
scope('static void ICAngles(', 'angles')
scope('\ncomputeOrbDescriptors(', 'descriptor')
scope('static void computeKeyPoints(', 'keypoints')
scope('void ORB_Impl::detectAndCompute(', 'total','++orbprof::stats.calls;orbprof::stats.provided+=useProvidedKeypoints;')
once('        fd->detect(img, keypoints, mask);','        { orbprof::Scope profile_fast(orbprof::stats.fast);\n        fd->detect(img, keypoints, mask); }')
once('    Mat prevImg = image, prevMask = mask;','    { orbprof::Scope profile_pyramid(orbprof::stats.pyramid);\n    Mat prevImg = image, prevMask = mask;')
once('    if( useOCL )\n        copyVectorToUMat(layerOfs, ulayerInfo);','    }\n    if( useOCL )\n        copyVectorToUMat(layerOfs, ulayerInfo);')
once('            GaussianBlur(workingMat, workingMat, Size(7, 7), 2, 2, BORDER_REFLECT_101);',
     '            orbprof::Scope profile_blur(orbprof::stats.blur);\n            GaussianBlur(workingMat, workingMat, Size(7, 7), 2, 2, BORDER_REFLECT_101);')
out.parent.mkdir(parents=True,exist_ok=True)
once('#include "precomp.hpp"', '#include "precomp.hpp"\n'
     'bool tidesense_orb_ive_detect(const cv::Mat&,const cv::Mat&,std::vector<cv::KeyPoint>&) __attribute__((weak));')
once('        fd->detect(img, keypoints, mask); }',
     '        if(!tidesense_orb_ive_detect || !tidesense_orb_ive_detect(img,mask,keypoints))\n'
     '            fd->detect(img, keypoints, mask); }')
once('        // Remove keypoints very close to the border',
     '        if(level<32){++orbprof::stats.level_calls[level];orbprof::stats.raw[level]+=keypoints.size();}\n'
     '        // Remove keypoints very close to the border')
once('        // Keep more points than necessary as FAST does not give amazing corners',
     '        if(level<32)orbprof::stats.border[level]+=keypoints.size();\n'
     '        // Keep more points than necessary as FAST does not give amazing corners')
once('        counters[level] = nkeypoints;',
     '        counters[level] = nkeypoints;\n'
     '        if(level<32)orbprof::stats.retained[level]+=keypoints.size();')
once('        std::swap(allKeypoints, newAllKeypoints);\n    }',
     '        std::swap(allKeypoints, newAllKeypoints);\n    }\n'
     '    for(const auto& key:allKeypoints)if(key.octave>=0 && key.octave<32)++orbprof::stats.final[key.octave];')
out.write_text(preamble+text)
print(out)

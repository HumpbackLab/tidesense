#!/usr/bin/env python3
"""Isolate frontend batching/RANSAC and optional FP32 Householder nullspace changes."""
import argparse
from pathlib import Path
import shutil

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('work',type=Path)
parser.add_argument('--base',type=Path,default=Path('/home/ncer/sophpi/install/rv1103-backend-20261025'))
args=parser.parse_args()
ov=args.work/'algorithm/open_vins'
if ov.exists(): raise SystemExit(f'Refusing to overwrite {ov}')
source=args.base/'src/third_party/open_vins'
track=ov/'ov_core/src/track'
track.mkdir(parents=True,exist_ok=True)
shutil.copy2(source/'ov_core/src/track/TrackKLT.h',track/'TrackKLT.h')
p=track/'TrackKLT.cpp'
text=(source/'ov_core/src/track/TrackKLT.cpp').read_text()
text='#include "cam/CamRadtan.h"\n#include "cam/CamEqui.h"\n#include <cstdlib>\n#include <cstring>\n#include <cmath>\n'+text
old='''  std::vector<cv::Point2f> pts0_n, pts1_n;
  for (size_t i = 0; i < pts0.size(); i++) {
    pts0_n.push_back(camera_calib.at(id0)->undistort_cv(pts0.at(i)));
    pts1_n.push_back(camera_calib.at(id1)->undistort_cv(pts1.at(i)));
  }'''
new='''  std::vector<cv::Point2f> candidate0, candidate1, pts0_n, pts1_n;
  std::vector<size_t> candidate_indices;
  const char *valid_only=std::getenv("OPENVINS_RANSAC_VALID_ONLY");
  const bool filter_valid=valid_only && std::strcmp(valid_only,"1")==0;
  for(size_t i=0;i<pts0.size();++i) {
    if(filter_valid && (!mask_klt.at(i) || !std::isfinite(pts0[i].x) || !std::isfinite(pts0[i].y) ||
                        !std::isfinite(pts1[i].x) || !std::isfinite(pts1[i].y))) continue;
    candidate_indices.push_back(i); candidate0.push_back(pts0[i]); candidate1.push_back(pts1[i]);
  }
  if(candidate0.size()<10) {
    mask_out.insert(mask_out.end(),pts0.size(),0);
    return;
  }
  auto normalize=[&](const std::vector<cv::Point2f> &points, size_t camera, std::vector<cv::Point2f> &output) {
    const auto &model=camera_calib.at(camera);
    const char *batch=std::getenv("OPENVINS_BATCH_NORMALIZE");
    if(batch && std::strcmp(batch,"1")==0 && std::dynamic_pointer_cast<CamRadtan>(model))
      cv::undistortPoints(points,output,model->get_K(),model->get_D());
    else if(batch && std::strcmp(batch,"1")==0 && std::dynamic_pointer_cast<CamEqui>(model))
      cv::fisheye::undistortPoints(points,output,model->get_K(),model->get_D());
    else for(const auto &point:points) output.push_back(model->undistort_cv(point));
    const char *verify=std::getenv("OPENVINS_VERIFY_NORMALIZE");
    if(verify && std::strcmp(verify,"1")==0) {
      for(size_t i=0;i<points.size();++i) {
        auto reference=model->undistort_cv(points[i]);
        if(reference.x!=output[i].x || reference.y!=output[i].y)
          throw std::runtime_error("batch normalization differs from original");
      }
    }
  };
  normalize(candidate0,id0,pts0_n); normalize(candidate1,id1,pts1_n);'''
assert text.count(old)==1
text=text.replace(old,new)
old='  cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0 / max_focallength, 0.999, mask_rsc);'
new='''  int max_iterations=0;
  if(const char *value=std::getenv("OPENVINS_RANSAC_MAX_ITERS")) {
    char *end=nullptr; long count=std::strtol(value,&end,10);
    if(*end || count<0 || count>2000) throw std::runtime_error("invalid RANSAC iteration limit");
    max_iterations=static_cast<int>(count);
  }
  std::vector<uchar> candidate_mask;
  if(max_iterations>0)
    cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0/max_focallength, 0.999, max_iterations, candidate_mask);
  else
    cv::findFundamentalMat(pts0_n, pts1_n, cv::FM_RANSAC, 2.0/max_focallength, 0.999, candidate_mask);
  mask_rsc.assign(pts0.size(),0);
  for(size_t i=0;i<candidate_indices.size() && i<candidate_mask.size();++i)
    mask_rsc[candidate_indices[i]]=candidate_mask[i];'''
assert text.count(old)==1
p.write_text(text.replace(old,new))

target=ov/'fp32/update'
shutil.copytree(source/'fp32/update',target)
p=target/'UpdaterHelper.cpp'
text=p.read_text()
old='''void UpdaterHelper::nullspace_project_inplace(Eigen::MatrixXf &H_f, Eigen::MatrixXf &H_x, Eigen::VectorXf &res) {
'''
new=old+'''
  const char *method=std::getenv("OPENVINS_NULLSPACE");
  if(method && std::strcmp(method,"householder")==0) {
    assert(H_f.rows()>=H_f.cols());
    Eigen::HouseholderQR<Eigen::MatrixXf> qr(H_f);
    Eigen::MatrixXf projected_H=(qr.householderQ().adjoint()*H_x).eval();
    Eigen::VectorXf projected_res=(qr.householderQ().adjoint()*res).eval();
    const int remaining=H_f.rows()-H_f.cols();
    H_x=projected_H.bottomRows(remaining).eval();
    res=projected_res.tail(remaining).eval();
    H_f=qr.matrixQR().triangularView<Eigen::Upper>();
    return;
  }
'''
assert text.count(old)==1
end=text.index('void UpdaterHelper::measurement_compress_inplace',text.index(old))
original=text[text.index(old)+len(old):end].rstrip()
assert original.endswith('}')
original=original[:-1]
new += '''  if(method && std::strcmp(method,"float-givens")==0) {
''' + original.replace('Eigen::JacobiRotation<double>', 'Eigen::JacobiRotation<float>') + '''
    return;
  }
'''
p.write_text(text.replace(old,new))

target=ov/'fp32/state'
shutil.copytree(source/'fp32/state',target)
p=target/'StateHelper.cpp'
text=p.read_text()
start=text.index('void StateHelper::EKFUpdate(')
end=text.index('void StateHelper::set_initial_covariance(',start)
section=text[start:end]
old='''  Eigen::MatrixXf Sinv = Eigen::MatrixXf::Identity(R.rows(), R.rows());
  S.selfadjointView<Eigen::Upper>().llt().solveInPlace(Sinv);
  Eigen::MatrixXf K = M_a * Sinv.selfadjointView<Eigen::Upper>();'''
new='''  Eigen::MatrixXf K;
  const char *gain_solve=std::getenv("OPENVINS_GAIN_SOLVE");
  if(gain_solve && std::strcmp(gain_solve,"direct")==0) {
    Eigen::MatrixXf gain_transpose=M_a.transpose();
    S.selfadjointView<Eigen::Upper>().llt().solveInPlace(gain_transpose);
    K=gain_transpose.transpose();
  } else {
    Eigen::MatrixXf Sinv = Eigen::MatrixXf::Identity(R.rows(), R.rows());
    S.selfadjointView<Eigen::Upper>().llt().solveInPlace(Sinv);
    K = M_a * Sinv.selfadjointView<Eigen::Upper>();
  }'''
assert section.count(old)==1
p.write_text(text[:start]+section.replace(old,new)+text[end:])
print(ov)

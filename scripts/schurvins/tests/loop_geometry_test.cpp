// SPDX-License-Identifier: GPL-3.0-or-later
#include <svo/online_loopclosing/geometric_verification.h>
#include <svo/online_loopclosing/map_alignment.h>
#include <svo/common/camera.h>
#include <vikit/cameras/ncamera.h>
#include <Eigen/Geometry>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: loop_geometry_test CALIBRATION.yaml");
    auto rig = svo::CameraBundle::loadFromYaml(argv[1]);
    if (!rig) throw std::runtime_error("camera rig load failed");
    const auto camera = rig->getCameraShared(0);
    const auto intrinsics = camera->getIntrinsicParameters();
    cv::Mat K = (cv::Mat_<double>(3,3) << intrinsics[0],0,intrinsics[2],0,intrinsics[1],intrinsics[3],0,0,1);
    const Eigen::Matrix3d rotation = Eigen::AngleAxisd(0.07,Eigen::Vector3d::UnitY()).toRotationMatrix();
    const Eigen::Vector3d translation(0.25,0.03,0.01);
    std::vector<cv::Point2f> first, second;
    svo::BearingVecs bearings1, bearings2;
    // Non-planar 3D scene: half new ORB pixels, half existing VIO bearings.
    for (int i=0;i<80;++i) {
      Eigen::Vector3d p((i%10-4.5)*0.2,(i/10-3.5)*0.15,2.0+(i*7%19)*0.13);
      Eigen::Vector3d q = rotation*p+translation;
      Eigen::Vector2d a,b;
      camera->project3(p,&a); camera->project3(q,&b);
      first.emplace_back(a.x(),a.y()); second.emplace_back(b.x(),b.y());
      if(i>=40) { bearings1.push_back(p.normalized()); bearings2.push_back(q.normalized()); }
    }
    Eigen::MatrixXd matches(2,first.size());
    for(size_t i=0;i<first.size();++i) matches(0,i)=matches(1,i)=i;
    std::vector<cv::Point2f> a(first.size()),b(first.size()),au(first.size()),bu(first.size());
    cv::Mat inliers; Eigen::MatrixXd relative;
    svo::geometricVerification(first,second,bearings1,bearings2,matches,K,camera->getDistortionParameters(),
      40,40,false,&inliers,&a,&b,&au,&bu,&relative,camera);
    const int count=cv::countNonZero(inliers);
    if(count<75 || relative.rows()!=4 || (relative.block<3,3>(0,0)-rotation).norm()>0.01)
      throw std::runtime_error("camera-model loop geometry regression");
    const double rotation_error=(relative.block<3,3>(0,0)-rotation).norm();
    svo::MapAlignmentOptions options;
    options.ransac3d_min_pts=8; options.ransac3d_inlier_percent=40;
    svo::MapAlignmentSE3 alignment(options);
    for(int i=0;i<40;++i) {
      Eigen::Vector3d point((i%10)*0.13,(i/10)*0.21,(i*7%13)*0.15);
      alignment.addCorrespondencePair(point,rotation*point+translation);
    }
    svo::Transformation transform; std::vector<int> verified3d;
    if(!alignment.getTransformRansac(10,false,&transform,&verified3d) || verified3d.size()!=40 ||
        (transform.getRotationMatrix()-rotation).norm()>1e-6 ||
        (transform.getPosition()-translation).norm()>1e-6)
      throw std::runtime_error("3D loop alignment regression");
    // Collinear pixels cannot yield a usable essential matrix; reject without recoverPose crashing.
    std::fill(first.begin(),first.end(),cv::Point2f(200,200));
    std::fill(second.begin(),second.end(),cv::Point2f(200,200));
    svo::geometricVerification(first,second,{}, {},matches,K,camera->getDistortionParameters(),
      80,80,false,&inliers,&a,&b,&au,&bu,&relative,camera);
    std::cout << "LOOP GEOMETRY PASS: inliers2d=" << count << " rotation_error="
      << rotation_error << " inliers3d=" << verified3d.size() << '\n';
    return 0;
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

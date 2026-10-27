// SPDX-License-Identifier: GPL-3.0-or-later
#include "depth.hpp"
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) try {
  cv::setNumThreads(1);
  cv::Mat disparity(1,100,CV_16SC1,cv::Scalar(-16)), valid(1,100,CV_8U,cv::Scalar(255));
  disparity.at<int16_t>(0,60)=160; // fx=200px, B=50mm, d=10px => 1000mm
  disparity.at<int16_t>(0,61)=80;
  disparity.at<int16_t>(0,62)=0;
  disparity.at<int16_t>(0,2)=160; // Matched position outside image.
  auto depth=StereoDepth::millimetres(disparity,10,valid,valid);
  require(depth.at<uint16_t>(0,60)==1000 && depth.at<uint16_t>(0,61)==2000,"Metric scale error");
  require(!depth.at<uint16_t>(0,62) && !depth.at<uint16_t>(0,2) && !depth.at<uint16_t>(0,59),"Invalid disparity error");
  auto target=valid.clone(); target.at<uint8_t>(0,50)=0;
  depth=StereoDepth::millimetres(disparity,10,valid,target);
  require(!depth.at<uint16_t>(0,60),"Invalid target support accepted");
  disparity.at<int16_t>(0,60)=1;
  require(!StereoDepth::millimetres(disparity,10,valid,valid).at<uint16_t>(0,60),"Overflow depth accepted");
  if (argc>1) {
    StereoDepth actual(argv[1],320,32);
    const auto& i=actual.intrinsics();
    require(i.reference==1 && i.baseline>0.049 && i.baseline<0.051 && i.fx>100,"Reference-board calibration error");
    try { StereoDepth invalid(argv[1],320,31); throw std::logic_error("Invalid disparity range accepted"); }
    catch (const std::runtime_error&) {}
  }
  char path[]="/tmp/tidesense-depth-test-XXXXXX";
  int fd=mkstemp(path); require(fd>=0,"Cannot create fixture"); close(fd);
  try {
    YAML::Node n;
    for (int side=0; side<2; ++side) {
      YAML::Node c;
      c["camera"]["type"]="pinhole";
      c["camera"]["image_width"]=640; c["camera"]["image_height"]=400;
      auto matrix=[](int rows,int cols,std::vector<double> data) {
        YAML::Node m; m["rows"]=rows; m["cols"]=cols; m["data"]=data; return m;
      };
      c["camera"]["intrinsics"]=matrix(4,1,{320,320,319.5,199.5});
      c["camera"]["distortion"]["type"]="radial-tangential";
      c["camera"]["distortion"]["parameters"]=matrix(4,1,{0,0,0,0});
      const double sign=side ? -1 : 1;
      c["T_B_C"]=matrix(4,4,{sign,0,0,side ? -.05 : 0,0,sign,0,0,0,0,1,0,0,0,0,1});
      n["cameras"].push_back(c);
    }
    { std::ofstream f(path); f << n; }
    cv::Mat left(400,640,CV_8U), upright(left.size(),CV_8U,cv::Scalar(0)), right;
    cv::RNG rng(42); rng.fill(left,cv::RNG::UNIFORM,0,256);
    left(cv::Rect(0,0,624,400)).copyTo(upright(cv::Rect(16,0,624,400)));
    cv::flip(upright,right,-1);
    for (int width : {320,640}) {
      StereoDepth engine(path,width,width==320 ? 32 : 64);
      const auto& i=engine.intrinsics();
      require(i.reference==1,"Wrong positive-disparity reference");
      auto z=engine.compute(left,right);
      DepthTiming timing;
      auto profiled=engine.compute(left,right,&timing);
      require(cv::norm(z,profiled,cv::NORM_INF)==0,"Profiling changed depth pixels");
      for (int stage=1; stage<4; ++stage)
        require(timing.wall[stage]>=timing.wall[stage-1] && timing.cpu[stage]>=timing.cpu[stage-1],
                "Nonmonotonic profiling clocks");
      std::vector<uint16_t> central;
      for (int y=z.rows/4; y<z.rows*3/4; ++y)
        for (int x=z.cols/3; x<z.cols*3/4; ++x) if (z.at<uint16_t>(y,x)) central.push_back(z.at<uint16_t>(y,x));
      require(central.size()>size_t(z.total()/10),"Insufficient synthetic plane coverage");
      std::sort(central.begin(),central.end());
      const double expected=i.fx*i.baseline*1000/(16.*width/640);
      require(std::abs(central[central.size()/2]-expected)<expected*.05,"Synthetic plane distance error");
      require(cv::countNonZero(engine.compute(cv::Mat::zeros(left.size(),CV_8U),cv::Mat::zeros(left.size(),CV_8U)))==0,
              "Textureless image produced depth");
    }
  } catch (...) { std::remove(path); throw; }
  std::remove(path);
  std::cout << "PASS metric depth, invalid pixels, calibration, orientation, synthetic plane at both sizes\n";
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

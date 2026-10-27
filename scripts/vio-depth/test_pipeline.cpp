// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline.hpp"
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char** argv) try {
  cv::setNumThreads(1);
  char path[]="/tmp/joint-depth-test-XXXXXX";int fd=mkstemp(path);
  require(fd>=0,"Cannot create calibration fixture");close(fd);
  try {
    YAML::Node n;
    auto matrix=[](int rows,int cols,std::vector<double> data) {
      YAML::Node m;m["rows"]=rows;m["cols"]=cols;m["data"]=data;return m;
    };
    for(int side=0;side<2;++side) {
      YAML::Node c;c["camera"]["type"]="pinhole";c["camera"]["image_width"]=640;c["camera"]["image_height"]=400;
      c["camera"]["intrinsics"]=matrix(4,1,{320,320,319.5,199.5});
      c["camera"]["distortion"]["type"]="radial-tangential";
      c["camera"]["distortion"]["parameters"]=matrix(4,1,{0,0,0,0});
      double s=side?-1:1;c["T_B_C"]=matrix(4,4,{s,0,0,side?-.05:0,0,s,0,0,0,0,1,0,0,0,0,1});
      n["cameras"].push_back(c);
    }
    { std::ofstream f(path);f<<n; }
    DepthPipeline pipeline(path);
    require(pipeline.intrinsics().reference==1,"Wrong physical depth reference");
    cv::Mat left(400,640,CV_8U),upright(left.size(),CV_8U,cv::Scalar(0)),right;
    cv::RNG rng(912);rng.fill(left,cv::RNG::UNIFORM,0,256);
    left(cv::Rect(0,0,624,400)).copyTo(upright(cv::Rect(16,0,624,400)));cv::flip(upright,right,-1);
    auto original=left.clone();
    cv::Mat reduced[2];cv::resize(left,reduced[0],cv::Size(320,200),0,0,cv::INTER_AREA);
    cv::resize(right,reduced[1],cv::Size(320,200),0,0,cv::INTER_AREA);
    StereoDepth geometry(path,320,32,true);
    auto fast=geometry.rectify(reduced[0],reduced[1]);fast[0]=fast[0].clone();fast[1]=fast[1].clone();
    auto generic=geometry.rectify(reduced[0],reduced[1],false);
    for(int i=0;i<2;++i) require(cv::countNonZero(fast[i]!=generic[i])==0,"Reduced bilinear map mismatch");
    PipelineTiming prep_time;auto prepared=pipeline.prepare(left,right,prep_time);
    for(int i=0;i<2;++i) {
      cv::Mat dx,expected;cv::Sobel(generic[1-i],dx,CV_16S,1,0,3,1,0,cv::BORDER_REFLECT_101);
      cv::max(dx,-31,dx);cv::min(dx,31,dx);dx.convertTo(expected,CV_8U,1,31);
      require(cv::countNonZero(prepared[i]!=expected)==0,"NEON XSobel mismatch");
    }
    double cpu=0,wall=0;
    for(int run=0;run<12;++run) {
      PipelineTiming t;auto z=pipeline.compute(left,right,t);
      std::vector<uint16_t> values;
      for(int y=40;y<160;++y) for(int x=80;x<270;++x) if(z.at<uint16_t>(y,x)) values.push_back(z.at<uint16_t>(y,x));
      require(values.size()>20000,"Known plane lost coverage");std::sort(values.begin(),values.end());
      require(std::abs(values[values.size()/2]-1000)<50,"Plane metric distance error");
      require(cv::countNonZero(original!=left)==0,"Original VIO image mutated");
      if(run>=2) { cpu+=t.cpu;wall+=t.downsample+t.rectify+t.prefilter+t.match+t.filter; }
    }
    PipelineTiming t;auto flat=cv::Mat::zeros(400,640,CV_8U);
    require(cv::countNonZero(pipeline.compute(flat,flat,t))==0,"Textureless image accepted");
    // A repeating eight-pixel reduced pattern has several equal disparity minima.
    cv::Mat stripe(400,16,CV_8U);rng.fill(stripe,cv::RNG::UNIFORM,0,256);
    for(int x=0;x<640;x+=16) stripe.copyTo(left(cv::Rect(x,0,16,400)));
    cv::flip(left,right,-1);
    require(cv::countNonZero(pipeline.compute(left,right,t))==0,"Ambiguous periodic texture accepted");
    std::cout << "PASS TPU resize, calibrated opposite orientation, plane 1000mm, original immutability, flat/ambiguous rejection; mean_wall_ms="
      << wall/10 << " mean_thread_cpu_ms=" << cpu/10 << '\n';
  } catch(...) { std::remove(path);throw; }
  std::remove(path);
  if(argc>1) {
    StereoDepth geometry(argv[1],320,32,true);cv::Mat a(200,320,CV_8U),b;
    cv::RNG rng(1251);rng.fill(a,cv::RNG::UNIFORM,0,256);b=a.clone();
    auto fast=geometry.rectify(a,b);fast[0]=fast[0].clone();fast[1]=fast[1].clone();
    auto generic=geometry.rectify(a,b,false);
    for(int i=0;i<2;++i) require(cv::countNonZero(fast[i]!=generic[i])==0,"Calibrated bilinear pixels differ");
    std::cout << "PASS real calibration bilinear equality, including fractional maps and borders\n";
  }
  return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; }

// SPDX-License-Identifier: GPL-3.0-or-later
// Compare this executable's CSV against a DEPTH_STEREOBM_ALL64=OFF build.
#include "depth.hpp"
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

uint64_t hash(const cv::Mat& m) {
  uint64_t h=14695981039346656037ULL;
  for (int y=0; y<m.rows; ++y) for (size_t x=0; x<size_t(m.cols)*m.elemSize(); ++x) {
    h^=m.ptr<uint8_t>(y)[x]; h*=1099511628211ULL;
  }
  return h;
}
cv::Mat pgm(const std::string& path) {
  std::ifstream f(path,std::ios::binary);
  std::string magic; int width=0,height=0,max=0;
  f >> magic >> width >> height >> max;
  if (!f || magic!="P5" || width!=640 || height!=400 || max!=255 || f.get()!='\n')
    throw std::runtime_error("Invalid fixture PGM: "+path);
  cv::Mat m(height,width,CV_8U);
  f.read(reinterpret_cast<char*>(m.data),m.total());
  if (!f) throw std::runtime_error("Truncated fixture PGM");
  return m;
}
int main(int argc,char**argv) try {
  if (argc<3 || argc>4) throw std::runtime_error("verify_matcher CALIBRATION OUTPUT.csv [FIXTURE_DIR]");
  cv::setNumThreads(1); cv::setUseOptimized(true);
  std::ofstream out(argv[2]); out.exceptions(std::ios::badbit|std::ios::failbit);
  out << "kind,width,case,disparities,fnv64,valid_pixels\n";
  for (int width:{320,640}) {
    cv::RNG rng(9123+width);
    for (int n=0; n<60; ++n) {
      const int nd=16*(1+(n%4)), shift=1+n%(nd-1);
      cv::Mat reference(width*5/8,width,CV_8U), target(reference.size(),CV_8U,cv::Scalar(0));
      rng.fill(reference,cv::RNG::UNIFORM,0,256);
      reference(cv::Rect(shift,0,width-shift,reference.rows)).copyTo(target(cv::Rect(0,0,width-shift,reference.rows)));
      if (n%6==0) { reference.setTo(0); target.setTo(0); }
      if (n%6==1) target(cv::Rect(width/3,0,width/5,target.rows)).setTo(0);
      if (n%6==2) rng.fill(target,cv::RNG::UNIFORM,0,256);
      if (n%6==3) { cv::GaussianBlur(reference,reference,cv::Size(5,5),0); cv::GaussianBlur(target,target,cv::Size(5,5),0); }
      if (n%6==4) { target.setTo(127); reference.setTo(127); }
      auto bm=cv::StereoBM::create(nd,15);
      bm->setMinDisparity(0); bm->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);
      bm->setPreFilterCap(31); bm->setTextureThreshold(10); bm->setUniquenessRatio(15);
      bm->setSpeckleWindowSize(100); bm->setSpeckleRange(2); bm->setDisp12MaxDiff(1);
      cv::Mat disparity;
      bm->compute(reference,target,disparity);
      out << "raw," << width << ',' << n << ',' << nd << ',' << hash(disparity)
          << ',' << cv::countNonZero(disparity>0) << '\n';
    }
    if (argc==4) {
      const int nd=width==320 ? 32 : 64;
      StereoDepth engine(argv[1],width,nd);
      std::ifstream csv(std::string(argv[3])+"/pairs.csv");
      if (!csv) throw std::runtime_error("Cannot open fixture pairs.csv");
      std::string line; int n=0;
      while (std::getline(csv,line)) {
        auto comma=line.find(',');
        if (comma==std::string::npos) throw std::runtime_error("Invalid fixture row");
        auto left=pgm(std::string(argv[3])+"/"+line.substr(0,comma));
        auto right=pgm(std::string(argv[3])+"/"+line.substr(comma+1));
        auto depth=engine.compute(left,right);
        out << "depth," << width << ',' << n++ << ',' << nd << ',' << hash(depth)
            << ',' << cv::countNonZero(depth) << '\n';
      }
    }
  }
  out.close();
  std::cout << "Matcher/depth fingerprints written\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

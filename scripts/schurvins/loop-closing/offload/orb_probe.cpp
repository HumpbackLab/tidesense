// SPDX-License-Identifier: GPL-3.0-or-later
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
extern "C" void tidesense_orb_profile_report() __attribute__((weak));
uint64_t checksum(const cv::Mat& d) {
  uint64_t hash=1469598103934665603ull;
  for(int i=0;i<d.rows;++i)for(int j=0;j<d.cols;++j)hash=(hash^d.at<uint8_t>(i,j))*1099511628211ull;
  return hash;
}
int main(int argc,char** argv) {
 try {
  if(argc<2 || argc>3)throw std::runtime_error("usage: orb_probe IMAGE [REPEATS]");
  const int repeats=argc==3?std::stoi(argv[2]):10;
  cv::Mat image=cv::imread(argv[1],cv::IMREAD_GRAYSCALE);
  if(image.empty() || repeats<1 || repeats>1000)throw std::runtime_error("Invalid image/repeats");
  auto orb=cv::ORB::create();std::vector<cv::KeyPoint> keys;cv::Mat descriptor;
  const auto begin=std::chrono::steady_clock::now();
  for(int i=0;i<repeats;++i) {
    // Match loop input: two detect+compute calls, plus mapped-point descriptors.
    orb->detectAndCompute(image,cv::Mat(),keys,descriptor);
    const auto hash=checksum(descriptor);const auto count=keys.size();
    orb->detectAndCompute(image,cv::Mat(),keys,descriptor);
    if(checksum(descriptor)!=hash || keys.size()!=count)throw std::runtime_error("ORB repeat mismatch");
    if(keys.size()>120)keys.resize(120);
    for(auto& key:keys) {key.octave=0;key.size=1;key.angle=-1;}
    orb->compute(image,keys,descriptor);
    if(i==0)std::cout<<"ORB_RESULT detected="<<count<<" detect_hash="<<hash<<" mapped="<<keys.size()<<" mapped_hash="<<checksum(descriptor)<<'\n';
  }
  std::cout<<"ORB_ELAPSED repeats="<<repeats<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
  if(tidesense_orb_profile_report)tidesense_orb_profile_report();
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
extern "C" void tidesense_orb_profile_report() __attribute__((weak));
extern "C" void tidesense_orb_blur_report() __attribute__((weak));
uint64_t checksum(const cv::Mat& d) {
  uint64_t hash=1469598103934665603ull;
  for(int i=0;i<d.rows;++i)for(int j=0;j<d.cols;++j)hash=(hash^d.at<uint8_t>(i,j))*1099511628211ull;
  return hash;
}
void compareDescriptors(const char* kind,const cv::Mat& gpu,const cv::Mat& cpu) {
  if(gpu.size()!=cpu.size() || gpu.type()!=cpu.type())throw std::runtime_error("Descriptor dimensions changed");
  unsigned changed=0,max_bits=0;double bits=0;
  for(int i=0;i<gpu.rows;++i){const unsigned d=cv::norm(gpu.row(i),cpu.row(i),cv::NORM_HAMMING);
    changed+=d!=0;max_bits=std::max(max_bits,d);bits+=d;}
  std::cout<<"ORB_DESCRIPTOR_DIFF kind="<<kind<<" rows="<<gpu.rows<<" changed_rows="<<changed
    <<" hamming_bits="<<bits<<" mean_bits="<<(gpu.rows?bits/gpu.rows:0)<<" max_bits="<<max_bits<<'\n';
}
int main(int argc,char** argv) {
 try {
  if(argc<2 || argc>3)throw std::runtime_error("usage: orb_probe IMAGE [REPEATS]");
  const int repeats=argc==3?std::stoi(argv[2]):10;
  cv::Mat image=cv::imread(argv[1],cv::IMREAD_GRAYSCALE);
  if(image.empty() || repeats<1 || repeats>1000)throw std::runtime_error("Invalid image/repeats");
  if(const char* mode=std::getenv("SCHUR_ORB_BLUR"))
    if(std::string(mode)!="cpu" && !tidesense_orb_blur_report)
      throw std::runtime_error("This ORB probe has no TPU Gaussian implementation");
  auto orb=cv::ORB::create();std::vector<cv::KeyPoint> keys;cv::Mat descriptor;
  const char* compare_option=std::getenv("SCHUR_ORB_COMPARE_CPU");
  if(compare_option && std::string(compare_option)!="0" && std::string(compare_option)!="1")
    throw std::runtime_error("SCHUR_ORB_COMPARE_CPU must be 0 or 1");
  const bool compare=compare_option && std::string(compare_option)=="1";
  const bool had_mode=std::getenv("SCHUR_ORB_BLUR");
  const std::string original_mode=had_mode?std::getenv("SCHUR_ORB_BLUR"):"";
  const auto restore_mode=[&]{if(had_mode)setenv("SCHUR_ORB_BLUR",original_mode.c_str(),1);else unsetenv("SCHUR_ORB_BLUR");};
  const auto begin=std::chrono::steady_clock::now();
  for(int i=0;i<repeats;++i) {
    // Match loop input: two detect+compute calls, plus mapped-point descriptors.
    orb->detectAndCompute(image,cv::Mat(),keys,descriptor);
    const auto hash=checksum(descriptor);const auto count=keys.size();
    if(compare && i==0) {
      std::vector<cv::KeyPoint> reference_keys;cv::Mat reference;
      setenv("SCHUR_ORB_BLUR","cpu",1);orb->detectAndCompute(image,cv::Mat(),reference_keys,reference);restore_mode();
      if(keys.size()!=reference_keys.size())throw std::runtime_error("Gaussian mode changed point count");
      for(size_t k=0;k<keys.size();++k) {
        const auto& a=keys[k];const auto& b=reference_keys[k];
        if(a.pt!=b.pt || a.octave!=b.octave || a.angle!=b.angle || a.response!=b.response || a.size!=b.size)
          throw std::runtime_error("Gaussian mode changed keypoints");
      }
      compareDescriptors("detected",descriptor,reference);
    }
    orb->detectAndCompute(image,cv::Mat(),keys,descriptor);
    if(checksum(descriptor)!=hash || keys.size()!=count)throw std::runtime_error("ORB repeat mismatch");
    if(keys.size()>120)keys.resize(120);
    for(auto& key:keys) {key.octave=0;key.size=1;key.angle=-1;}
    orb->compute(image,keys,descriptor);
    if(compare && i==0) {
      auto reference_keys=keys;cv::Mat reference;
      setenv("SCHUR_ORB_BLUR","cpu",1);orb->compute(image,reference_keys,reference);restore_mode();
      compareDescriptors("mapped",descriptor,reference);
    }
    if(i==0)std::cout<<"ORB_RESULT detected="<<count<<" detect_hash="<<hash<<" mapped="<<keys.size()<<" mapped_hash="<<checksum(descriptor)<<'\n';
  }
  std::cout<<"ORB_ELAPSED repeats="<<repeats<<" elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
  if(tidesense_orb_profile_report)tidesense_orb_profile_report();
  if(tidesense_orb_blur_report)tidesense_orb_blur_report();
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

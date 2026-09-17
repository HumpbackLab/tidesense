// SPDX-License-Identifier: GPL-3.0-or-later
// On-target correctness and timing of the actual production entry points.
#include <svo/direct/patch_score.h>
#include <svo/direct/fast10_neon.h>
#include <vikit/vision.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <random>
#include <functional>
#include <climits>
using Clock = std::chrono::steady_clock;
volatile uint64_t sink;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void detect(const cv::Mat& im, int threshold, std::vector<fast::fast_xy>& c) {
#if defined(SCHUR_OPT_FAST10_NEON) && SCHUR_OPT_FAST10_NEON
  svo::fast10DetectNEON(im.data, im.cols, im.rows, im.step, threshold, c);
#else
  fast::fast_corner_detect_10(im.data, im.cols, im.rows, im.step, threshold, c);
#endif
}
void score(const cv::Mat& im, const std::vector<fast::fast_xy>& c, int t, std::vector<int>& s) {
#if defined(SCHUR_OPT_FAST10_NEON) && SCHUR_OPT_FAST10_NEON
  svo::fast10ScoreNEON(im.data, im.step, c, t, s);
#else
  fast::fast_corner_score_10(im.data, im.step, c, t, s);
#endif
}
// Median of 7 timed batches; per-call milliseconds; no I/O in timed region.
double bench(const std::function<void()>& fn, int count) {
  for (int i=0;i<3;++i) fn();
  std::vector<double> times;
  for (int trial=0;trial<7;++trial) {
    const auto start=Clock::now();
    for(int i=0;i<count;++i) fn();
    times.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count()/count);
  }
  std::sort(times.begin(),times.end()); return times[3];
}
void check() {
  cv::RNG rng(7241);
  size_t images=0, patches=0;
  for(int h : {2,3,7,8,17,48,101}) for(int w : {2,3,6,7,8,13,21,22,23,31,32,37,64,97}) {
    cv::Mat storage(h+2,w+19,CV_8U); rng.fill(storage,cv::RNG::UNIFORM,0,256);
    cv::Mat im=storage(cv::Rect(1,1,w,h));
    cv::Mat output(h/2+2,w/2+11,CV_8U,cv::Scalar(171));
    cv::Mat dst=output(cv::Rect(3,1,w/2,h/2));
    vk::halfSample(im,dst);
    for(int y=0;y<h/2;++y) for(int x=0;x<w/2;++x)
      require(dst.at<uint8_t>(y,x)==(int(im.at<uint8_t>(2*y,2*x))+im.at<uint8_t>(2*y,2*x+1)+im.at<uint8_t>(2*y+1,2*x)+im.at<uint8_t>(2*y+1,2*x+1))/4,"halfSample pixel");
    for(int y=0;y<output.rows;++y) for(int x=0;x<output.cols;++x)
      if(y<1||y>=1+h/2||x<3||x>=3+w/2) require(output.at<uint8_t>(y,x)==171,"halfSample overwrite");
    for(int t : {0,1,10,20,80,127,254,255}) {
      std::vector<fast::fast_xy> ref,c;
      fast::fast_corner_detect_10(im.data,w,h,im.step,t,ref); detect(im,t,c);
      require(ref.size()==c.size(),"FAST corner count");
      for(size_t i=0;i<c.size();++i) require(ref[i].x==c[i].x&&ref[i].y==c[i].y,"FAST position/order");
      std::vector<int> a,b,na,nb;
      fast::fast_corner_score_10(im.data,im.step,ref,t,a); score(im,c,t,b);
      require(a==b,"FAST score"); fast::fast_nonmax_3x3(ref,a,na); fast::fast_nonmax_3x3(c,b,nb);
      require(na==nb,"FAST NMS");
    }
    ++images;
  }
  // Exhaust every binary ring pattern, both polarities, including vector lanes.
  const int ox[16]={0,1,2,3,3,3,2,1,0,-1,-2,-3,-3,-3,-2,-1};
  const int oy[16]={3,3,2,1,0,-1,-2,-3,-3,-3,-2,-1,0,1,2,3};
  for(unsigned mask=0;mask<65536;++mask) {
    cv::Mat im(7,14,CV_8U,cv::Scalar(128));
    for(int i=0;i<16;++i) im.at<uint8_t>(3+oy[i],6+ox[i])=(mask&(1u<<i))?255:0;
    std::vector<fast::fast_xy> a,b; detect(im,10,a);
    fast::fast_corner_detect_10(im.data,im.cols,im.rows,im.step,10,b);
    require(a.size()==b.size(),"FAST exhaustive count");
    for(size_t i=0;i<a.size();++i) require(a[i].x==b[i].x&&a[i].y==b[i].y,"FAST exhaustive position");
    std::vector<int> sa,sb;score(im,a,10,sa);fast::fast_corner_score_10(im.data,im.step,b,10,sb);
    require(sa==sb,"FAST exhaustive score");
  }
  for(int k=0;k<10000;++k) {
    uint8_t a[64]; cv::Mat b(8,19,CV_8U);
    for(auto& v:a) v=rng.uniform(0,256); rng.fill(b,cv::RNG::UNIFORM,0,256);
    if(k<4) {std::fill(a,a+64,k&1?255:0);b.setTo(k&2?255:0);}
    int sa=0,sb=0,saa=0,sbb=0,sab=0;
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
      int av=a[y*8+x],bv=b.at<uint8_t>(y,x+1);sa+=av;sb+=bv;saa+=av*av;sbb+=bv*bv;sab+=av*bv;
    }
    svo::patch_score::ZMSSD<4> p(a);
    require(p.sumA_==sa&&p.sumAA_==saa,"ZMSSD template sums");
    require(p.computeScore(b.data+1,b.step)==saa-2*sab+sbb-(sa*sa-2*sa*sb+sb*sb)/64,"ZMSSD stride score");
    cv::Mat packed=b(cv::Rect(1,0,8,8)).clone();
    require(p.computeScore(packed.data)==p.computeScore(b.data+1,b.step),"ZMSSD contiguous score");++patches;
  }
  std::cout<<"CHECK PASS images="<<images<<" rings=65536 patches="<<patches<<'\n';
}
int main(int argc,char**argv) {try {
  cv::setNumThreads(1); check();
  std::vector<std::pair<std::string,cv::Mat>> inputs;
  for(int i=1;i<argc;++i) {auto im=cv::imread(argv[i],cv::IMREAD_GRAYSCALE);require(!im.empty(),"image read");inputs.emplace_back(argv[i],im);}
  if(inputs.empty()) {cv::Mat im(480,752,CV_8U);cv::RNG r(42);r.fill(im,cv::RNG::UNIFORM,0,256);cv::GaussianBlur(im,im,cv::Size(3,3),0.8);inputs.emplace_back("synthetic",im);}
  std::cout<<"image,kernel,ms_per_call,count\n";
  for(auto& entry:inputs) {auto im=entry.second; const auto& name=entry.first;
    cv::Mat dst(im.rows/2,im.cols/2,CV_8U);
    std::cout<<name<<",halfSample,"<<bench([&]{vk::halfSample(im,dst);sink+=dst.data[0];},100)<<",1\n";
    std::vector<fast::fast_xy> c;detect(im,10,c);std::vector<int>s,n;
    std::cout<<name<<",fast_detect,"<<bench([&]{c.clear();detect(im,10,c);sink+=c.size();},10)<<','<<c.size()<<'\n';
    std::cout<<name<<",fast_score,"<<bench([&]{score(im,c,10,s);sink+=s.size();},20)<<','<<c.size()<<'\n';
    std::cout<<name<<",fast_total,"<<bench([&]{c.clear();detect(im,10,c);score(im,c,10,s);fast::fast_nonmax_3x3(c,s,n);sink+=n.size();},10)<<','<<c.size()<<'\n';
    uint8_t patch[64];for(int y=0;y<8;++y) std::copy(im.ptr<uint8_t>(y),im.ptr<uint8_t>(y)+8,patch+y*8);
    svo::patch_score::ZMSSD<4> p(patch);
    std::cout<<name<<",zmssd_1000,"<<bench([&]{uint64_t sum=0;for(int i=0;i<1000;++i){int x=i%(im.cols-8),y=(i*7)%(im.rows-8);sum+=p.computeScore(im.ptr<uint8_t>(y)+x,im.step);}sink+=sum;},100)<<",1000\n";
  }
  return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

// SPDX-License-Identifier: GPL-3.0-or-later
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <time.h>
#include "gaussian_cpu_q9.hpp"
bool tidesense_orb_blur(cv::Mat&,const std::vector<cv::Rect>&);
extern "C" void tidesense_orb_blur_report();
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::duration d){return std::chrono::duration<double,std::milli>(d).count();}
double cpu_ms(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.+t.tv_nsec*1e-6;}
void require(bool value,const char* what){if(!value)throw std::runtime_error(what);}
cv::Mat quantized(const cv::Mat& parent,const cv::Rect& r) {
  constexpr int k[7]={36,67,98,110,98,67,36};
  cv::Mat middle(r.height+6,r.width,CV_8U),out(r.size(),CV_8U);
  for(int y=0;y<r.height+6;++y)for(int x=0;x<r.width;++x) {
    unsigned sum=0;const auto* p=parent.ptr<uint8_t>(r.y+y-3)+r.x+x-3;
    for(int i=0;i<7;++i)sum+=p[i]*k[i];middle.at<uint8_t>(y,x)=(sum+256)>>9;
  }
  for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
    unsigned sum=0;for(int i=0;i<7;++i)sum+=middle.at<uint8_t>(y+i,x)*k[i];
    out.at<uint8_t>(y,x)=(sum+256)>>9;
  }
  return out;
}
void tests() {
  setenv("SCHUR_ORB_BLUR_VERIFY","1",1);unsigned cases=0;
  cv::RNG rng(9321);
  for(const auto& size:std::vector<cv::Size>{{1,1},{64,64},{533,333},{640,400}}) {
    for(int pattern=0;pattern<7;++pattern) {
      cv::Mat parent(size.height+8,size.width+8,CV_8U);
      rng.fill(parent,cv::RNG::UNIFORM,0,256);
      cv::Rect rect(4,4,size.width,size.height);cv::Mat roi=parent(rect);
      if(pattern<3)parent.setTo(pattern==0?0:pattern==1?73:255);
      else if(pattern<6) {
        for(int y=0;y<parent.rows;++y)for(int x=0;x<parent.cols;++x)
          parent.at<uint8_t>(y,x)=pattern==3?(x*3+y*7)%256:pattern==4?((x/8+y/8)%2)*255:((x+y)%2)*255;
      }
      cv::Mat expected=quantized(parent,rect);
      cv::Mat cpu_q9=parent.clone();schur_gaussian_cpu_q9(cpu_q9,{rect});
      require(cv::norm(cpu_q9(rect),expected,cv::NORM_INF)==0,"NEON Q9 differs from scalar model");
      require(tidesense_orb_blur(parent,{rect}),"TPU Gaussian hook not active");
      const double maximum=cv::norm(parent(rect),expected,cv::NORM_INF);
      if(maximum!=0){std::cerr<<"Q9 mismatch width="<<size.width<<" height="<<size.height
          <<" pattern="<<pattern<<" max="<<maximum<<'\n';throw std::runtime_error("TPU differs from scalar Q9 model");}
      ++cases;
    }
  }
  std::cout<<"GAUSSIAN FUNCTIONAL PASS cases="<<cases<<" including parent halos and odd dimensions\n";
}
void benchmark(const char* filename,int repeats) {
  const cv::Mat image=cv::imread(filename,cv::IMREAD_GRAYSCALE);
  require(!image.empty() && image.cols<=640 && image.rows<=480 && repeats>0 && repeats<=1000,"Invalid image or repeats");
  std::vector<cv::Rect> rectangles;int height=0;
  for(int level=0;level<8;++level){const double scale=std::pow(1.2,level);
    const cv::Size size(cvRound(image.cols/scale),cvRound(image.rows/scale));
    rectangles.emplace_back(4,height+4,size.width,size.height);height+=size.height+8;}
  cv::Mat original(height,image.cols+8,CV_8U,cv::Scalar(0));cv::Mat previous=image;
  for(const auto& r:rectangles) {
    cv::Mat resized;if(r.size()==image.size())resized=image;
    else cv::resize(previous,resized,r.size(),0,0,cv::INTER_LINEAR_EXACT);
    cv::Mat padded=original(cv::Rect(0,r.y-4,r.width+8,r.height+8));
    cv::copyMakeBorder(resized,padded,4,4,4,4,cv::BORDER_REFLECT_101);
    previous=original(r);
  }
  setenv("SCHUR_ORB_BLUR_VERIFY","0",1);
  cv::Mat working=original.clone();require(tidesense_orb_blur(working,rectangles),"TPU hook not active");
  schur_gaussian_cpu_q9(working,rectangles); // Warm reusable CPU scratch.
  double cpu_wall=0,tpu_wall=0,cpu_thread=0,tpu_thread=0,q9_wall=0,q9_thread=0;
  for(int i=0;i<repeats;++i) {
    original.copyTo(working);auto start=Clock::now();double thread=cpu_ms();
    for(const auto& r:rectangles){cv::Mat roi=working(r);cv::GaussianBlur(roi,roi,cv::Size(7,7),2,2,cv::BORDER_REFLECT_101);}
    cpu_thread+=cpu_ms()-thread;cpu_wall+=milliseconds(Clock::now()-start);
    original.copyTo(working);start=Clock::now();thread=cpu_ms();
    schur_gaussian_cpu_q9(working,rectangles);
    q9_thread+=cpu_ms()-thread;q9_wall+=milliseconds(Clock::now()-start);
    original.copyTo(working);start=Clock::now();thread=cpu_ms();
    require(tidesense_orb_blur(working,rectangles),"TPU hook not active");
    tpu_thread+=cpu_ms()-thread;tpu_wall+=milliseconds(Clock::now()-start);
  }
  std::cout<<"GAUSSIAN_BENCH repeats="<<repeats<<" layers=8 cpu_wall_ms="<<cpu_wall
    <<" tpu_wall_ms="<<tpu_wall<<" cpu_thread_ms="<<cpu_thread<<" tpu_thread_ms="<<tpu_thread
    <<" cpu_q9_wall_ms="<<q9_wall<<" cpu_q9_thread_ms="<<q9_thread<<'\n';
}
}
int main(int argc,char** argv) {
 try {
  setenv("SCHUR_ORB_BLUR","tpu-q9",1);
  if(argc==1)tests();
  else if(argc==2 || argc==3)benchmark(argv[1],argc==3?std::stoi(argv[2]):20);
  else throw std::runtime_error("usage: gaussian_probe [IMAGE [REPEATS]]");
  tidesense_orb_blur_report();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

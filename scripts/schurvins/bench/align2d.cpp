// On-target validation of production align2D, including its full iteration trace.
#include <svo/direct/feature_alignment.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace svo { namespace feature_alignment {
bool align2D_reference(const cv::Mat&, uint8_t*, uint8_t*, int, bool, bool,
                      Keypoint&, bool, std::vector<Eigen::Vector2f>*);
}}
using namespace svo;
struct Patch { uint8_t border[100], data[64]; Keypoint start; };
Patch patch(const cv::Mat& im, int x, int y, double dx, double dy) {
  Patch p;
  for(int j=0;j<10;++j) for(int i=0;i<10;++i) p.border[j*10+i]=im.at<uint8_t>(y+j-5,x+i-5);
  for(int j=0;j<8;++j) for(int i=0;i<8;++i) p.data[j*8+i]=p.border[(j+1)*10+i+1];
  p.start=Keypoint(x+dx,y+dy); return p;
}
bool equal(double a,double b) { return a==b || (std::isnan(a)&&std::isnan(b)); }
size_t cases=0;
void check(const cv::Mat& cur, Patch p, int n, bool offset, bool gain) {
  Keypoint a=p.start,b=a,c=a;
  std::vector<Eigen::Vector2f> sa,sb,sc;
  bool ra=feature_alignment::align2D_reference(cur,p.border,p.data,n,offset,gain,a,true,&sa);
  bool rb=feature_alignment::align2D(cur,p.border,p.data,n,offset,gain,b,false,&sb);
  bool rc=feature_alignment::align2D(cur,p.border,p.data,n,offset,gain,c,true,&sc);
  auto compare=[&](bool r,const Keypoint& q,const std::vector<Eigen::Vector2f>& steps) {
    if(r!=ra || !equal(a.x(),q.x()) || !equal(a.y(),q.y()) || sa.size()!=steps.size()) return false;
    for(size_t i=0;i<sa.size();++i) for(int k=0;k<2;++k) if(!equal(sa[i][k],steps[i][k])) return false;
    return true;
  };
  if(!compare(rb,b,sb)||!compare(rc,c,sc)) {
    std::cerr<<"Mismatch case="<<cases<<" n="<<n<<" offset="<<offset<<" gain="<<gain<<" ref="<<a.transpose()<<" opt="<<b.transpose()<<" steps="<<sa.size()<<","<<sb.size()<<"\n";
    throw std::runtime_error("align2D differs from scalar reference");
  }
  ++cases;
}
volatile double sink=0;
double bench(const cv::Mat& im,std::vector<Patch>& patches,bool scalar) {
  std::vector<double> times;
  for(int batch=0;batch<8;++batch) {
    const auto begin=std::chrono::steady_clock::now();
    for(int repeat=0;repeat<20;++repeat) for(auto& p:patches) {
      Keypoint q=p.start;
      feature_alignment::align2D(im,p.border,p.data,10,true,false,q,scalar);
      sink+=q.x();
    }
    const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count()/(20*patches.size());
    if(batch) times.push_back(us);
  }
  std::sort(times.begin(),times.end());return times[times.size()/2];
}
int main(int argc,char** argv) {
  try {
    cv::RNG rng(9281);
    for(int mode=0;mode<5;++mode) {
      cv::Mat storage(55,87,CV_8U); rng.fill(storage,cv::RNG::UNIFORM,0,256);
      cv::Mat ref=storage(cv::Rect(1,1,63,49));
      if(mode==1) cv::GaussianBlur(ref,ref,cv::Size(5,5),1.2);
      if(mode==2) ref.setTo(127);
      if(mode==3) for(int y=0;y<ref.rows;++y) for(int x=0;x<ref.cols;++x) ref.at<uint8_t>(y,x)=x*3;
      cv::Mat backing(55,89,CV_8U),cur=backing(cv::Rect(3,2,63,49)); ref.copyTo(cur);
      if(mode==4) { cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,0.35,0,1,-0.4);cv::warpAffine(ref,cur,transform,cur.size());cur.convertTo(cur,CV_8U,0.8,12); }
      for(int i=0;i<400;++i) {
        auto p=patch(ref,rng.uniform(5,ref.cols-5),rng.uniform(5,ref.rows-5),rng.uniform(-1.5,1.5),rng.uniform(-1.5,1.5));
        if(i<8) p.start=Keypoint(i<4?3.99:cur.cols-4.0, i%2?4.0:cur.rows-4.0);
        for(int flags=0;flags<4;++flags) for(int n:{0,1,3,10}) check(cur,p,n,flags&1,flags&2);
      }
    }
    std::cout<<"PASS exact status, position, iteration trace; cases="<<cases<<" (all affine modes, ROI stride, borders, flat/ramp/blur/random/shifted images)\n";
    for(int arg=1;arg<argc;++arg) {
      cv::Mat im=cv::imread(argv[arg],cv::IMREAD_GRAYSCALE);
      if(im.empty()) throw std::runtime_error("image load failed");
      std::vector<Patch> patches;
      for(int y=8;y<im.rows-8;y+=23) for(int x=8;x<im.cols-8;x+=29) {
        patches.push_back(patch(im,x,y,0.3,-0.25));
        for(int flags=0;flags<4;++flags) check(im,patches.back(),10,flags&1,flags&2);
      }
      double scalar=bench(im,patches,true),optimized=bench(im,patches,false);
      std::cout<<"image="<<argv[arg]<<" patches="<<patches.size()<<" scalar_us="<<scalar<<" opt_us="<<optimized<<" speedup="<<scalar/optimized<<"\n";
    }
    std::cout<<"PASS total_cases="<<cases<<"\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}

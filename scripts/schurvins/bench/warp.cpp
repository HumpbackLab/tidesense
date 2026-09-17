#include <svo/direct/patch_warp.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <cstring>
namespace svo { namespace warp_reference {
bool warpAffine(const Eigen::Matrix2d&, const cv::Mat&, const Eigen::Ref<Keypoint>&, int,int,int,uint8_t*);
}}
using namespace svo;
struct Case { Eigen::Matrix2d A; Keypoint p; int ref,search,half; };
size_t checks=0;
void check(const cv::Mat& im,Case& c) {
 uint8_t a[1030],b[1030];memset(a,173,sizeof(a));memset(b,173,sizeof(b));
 bool ra=warp_reference::warpAffine(c.A,im,c.p,c.ref,c.search,c.half,a+7);
 bool rb=warp::warpAffine(c.A,im,c.p,c.ref,c.search,c.half,b+7);
 if(ra!=rb || memcmp(a,b,sizeof(a))) {
  int at=0;while(at<1030 && a[at]==b[at])++at;
  std::cerr<<"Mismatch "<<checks<<" at="<<at<<" values="<<(at<1030?int(a[at]):-1)<<","<<(at<1030?int(b[at]):-1)<<" status="<<ra<<","<<rb<<" p="<<c.p.transpose()<<" levels="<<c.ref<<","<<c.search<<" A="<<c.A<<"\n";
  throw std::runtime_error("warp differs including partial writes/sentinels");
 }
 ++checks;
}
volatile unsigned sink=0;
double bench(const cv::Mat& im,std::vector<Case>& cases,bool scalar) {
 std::vector<double> times;uint8_t out[100];
 for(int batch=0;batch<8;++batch) {
  auto start=std::chrono::steady_clock::now();
  for(int r=0;r<30;++r) for(auto& c:cases) {
   bool ok=scalar?warp_reference::warpAffine(c.A,im,c.p,c.ref,c.search,c.half,out):warp::warpAffine(c.A,im,c.p,c.ref,c.search,c.half,out);
   sink+=ok?out[0]:0;
  }
  if(batch)times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/(30*cases.size()));
 }
 std::sort(times.begin(),times.end());return times[3];
}
int main(int argc,char**argv) {try {
 cv::RNG rng(22009281);
 for(int mode=0;mode<3;++mode) {
  cv::Mat storage(91,127,CV_8U);rng.fill(storage,cv::RNG::UNIFORM,0,256);
  cv::Mat im=storage(cv::Rect(1,2,101,83));if(mode==1)im.setTo(255);if(mode==2)im.setTo(0);
  for(int i=0;i<12000;++i) {
   double t=rng.uniform(-3.14,3.14),scale=rng.uniform(0.3,3.0);
   Case c;c.A<<std::cos(t)*scale,-std::sin(t)*scale+rng.uniform(-0.3,0.3),std::sin(t)*scale,std::cos(t)*scale;
   c.ref=i%3;c.search=(i/3)%3;c.half=i%10==0?i%8+1:5;
   c.p=Keypoint(rng.uniform(-2.0,103.0),rng.uniform(-2.0,85.0))*(1<<c.ref);
   if(i%8==0) {c.A.setIdentity();c.p=Keypoint(5+(i%90),5+(i%72)); c.p.x()+=i%16?1e-6:-1e-6;}
   check(im,c);
  }
  Case zero;zero.A.setZero();zero.p=Keypoint(20,20);zero.ref=zero.search=0;zero.half=5;check(im,zero);
  // Check output/source aliasing: optimized path must fall back.
  cv::Mat a=im.clone(),b=a.clone();Case identity;identity.A.setIdentity();identity.p=Keypoint(15.2,15.7);identity.ref=identity.search=0;identity.half=5;
  bool ra=warp_reference::warpAffine(identity.A,a,identity.p,0,0,5,a.data+10*a.step+10);
  bool rb=warp::warpAffine(identity.A,b,identity.p,0,0,5,b.data+10*b.step+10);
  if(ra!=rb || memcmp(a.data,b.data,a.total())) throw std::runtime_error("alias fallback mismatch");++checks;
 }
 std::cout<<"PASS synthetic cases="<<checks<<" exact status/full buffer incl. partial writes, ROI, tails, levels, aliasing\n";
 for(int arg=1;arg<argc;++arg) {
  cv::Mat im=cv::imread(argv[arg],cv::IMREAD_GRAYSCALE);if(im.empty())throw std::runtime_error("image load");
  std::vector<Case> cases;
  for(int y=15;y<im.rows-15;y+=23)for(int x=15;x<im.cols-15;x+=29) {
   Case c;c.A<<1.03,0.07,-0.05,0.98;c.p=Keypoint(x+0.31,y-0.28);c.ref=c.search=0;c.half=5;check(im,c);cases.push_back(c);
  }
  double a=bench(im,cases,true),b=bench(im,cases,false);
  std::cout<<"image="<<argv[arg]<<" patches="<<cases.size()<<" scalar_us="<<a<<" opt_us="<<b<<" speedup="<<a/b<<"\n";
 }
 std::cout<<"PASS total_cases="<<checks<<"\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}

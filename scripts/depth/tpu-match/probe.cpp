// SPDX-License-Identifier: GPL-3.0-or-later
#include "tpu_sad.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <time.h>

using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
uint64_t cpu_ns() {
  timespec t{};if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)) throw std::runtime_error("CPU clock failed");
  return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}
cv::Mat read_pgm(const std::string& path) {
  std::ifstream f(path,std::ios::binary);std::string magic;int w=0,h=0,max=0;
  f>>magic>>w>>h>>max;
  if (!f || magic!="P5" || w<1 || h<1 || max!=255 || f.get()!='\n') throw std::runtime_error("Bad PGM: "+path);
  cv::Mat image(h,w,CV_8U);f.read(reinterpret_cast<char*>(image.data),image.total());
  if (!f) throw std::runtime_error("Truncated PGM");
  return image;
}
cv::Mat prefilter(const cv::Mat& raw) {
  cv::Mat dx,result;
  cv::Sobel(raw,dx,CV_16S,1,0,3,1,0,cv::BORDER_REFLECT_101);
  cv::max(dx,-31,dx);cv::min(dx,31,dx);dx.convertTo(result,CV_8U,1,31);
  if (raw.rows%2) result.row(raw.rows-1).setTo(31);
  return result;
}
SadResult cpu_sad(const cv::Mat& ref,const cv::Mat& target,int nd) {
  SadResult result{cv::Mat(ref.size(),CV_16U,cv::Scalar(0)),cv::Mat(ref.size(),CV_8U,cv::Scalar(0))};
  cv::Rect valid(nd-1+7,7,ref.cols-(nd-1)-14,ref.rows-14);
  result.cost(valid).setTo(32767);
  for (int d=0;d<nd;++d) {
    cv::Mat difference,cost;
    cv::absdiff(ref(cv::Rect(d,0,ref.cols-d,ref.rows)),target(cv::Rect(0,0,ref.cols-d,ref.rows)),difference);
    cv::boxFilter(difference,cost,CV_16U,cv::Size(15,15),cv::Point(-1,-1),false,cv::BORDER_CONSTANT);
    for (int y=valid.y;y<valid.y+valid.height;++y) {
      auto best=result.cost.ptr<uint16_t>(y);auto disp=result.disparity.ptr<uint8_t>(y);auto c=cost.ptr<uint16_t>(y);
      for (int x=valid.x;x<valid.x+valid.width;++x) if (c[x-d]<best[x]) { best[x]=c[x-d];disp[x]=d; }
    }
  }
  return result;
}
int main(int argc,char**argv) try {
  int width=128,height=64,nd=16,runs=3,tw=64,th=0;
  bool tree=true;
  std::string kind="random",refpath,targetpath,csv;
  for (int i=1;i<argc;++i) {
    std::string option=argv[i];
    if (++i>=argc) throw std::runtime_error("Every option needs a value");
    std::string value=argv[i];
    if (option=="--width") width=std::stoi(value);
    else if (option=="--height") height=std::stoi(value);
    else if (option=="--disparities") nd=std::stoi(value);
    else if (option=="--runs") runs=std::stoi(value);
    else if (option=="--tile-width") tw=std::stoi(value);
    else if (option=="--tile-height") th=std::stoi(value);
    else if (option=="--aggregation") { if (value!="tree" && value!="direct") throw std::runtime_error("Use tree or direct aggregation");tree=value=="tree"; }
    else if (option=="--case") kind=value;
    else if (option=="--reference") refpath=value;
    else if (option=="--target") targetpath=value;
    else if (option=="--csv") csv=value;
    else throw std::runtime_error("Unknown option: "+option);
  }
  if (runs<1 || runs>1000 || width>640 || height>400 || width<32 || height<16)
    throw std::runtime_error("Invalid geometry/run count");
  cv::setNumThreads(1);
  auto initialized=Clock::now();
  if (!th) th=tree?10:16;
  TpuSad tpu(width,height,nd,tw,th,tree);
  const double init_ms=ms(initialized,Clock::now());
  auto bm=cv::StereoBM::create(((nd+15)/16)*16,15);
  bm->setPreFilterType(cv::StereoBM::PREFILTER_XSOBEL);bm->setPreFilterCap(31);
  bm->setTextureThreshold(10);bm->setUniquenessRatio(15);bm->setSpeckleWindowSize(100);
  bm->setSpeckleRange(2);bm->setDisp12MaxDiff(1);
  std::ofstream file;
  if (!csv.empty()) { file.open(csv);file.exceptions(std::ios::badbit|std::ios::failbit); }
  auto& out=csv.empty()?std::cout:file;
  out << "run,width,height,disparities,case,aggregation,prefilter_ms,upload_ms,execute_ms,download_ms,tpu_path_ms,tpu_cpu_ms,cpu_sad_ms,all64_bm_ms,cost_mismatches,disparity_mismatches,plane_correct_fraction,bm_valid_pixels,wta_bm_integer_agreement,wta_positive_pixels\n" << std::fixed << std::setprecision(5);
  cv::Mat realref,realtarget;
  if (!refpath.empty() || !targetpath.empty()) {
    realref=read_pgm(refpath);realtarget=read_pgm(targetpath);
    if (realref.size()!=cv::Size(width,height) || realtarget.size()!=realref.size()) throw std::runtime_error("Wrong PGM dimensions");
  }
  for (int n=0;n<runs+2;++n) {
    cv::Mat rawref(height,width,CV_8U),rawtarget(height,width,CV_8U,cv::Scalar(0));
    cv::RNG rng(9123+n);rng.fill(rawref,cv::RNG::UNIFORM,0,256);
    int shift=std::min(nd-1,3+n%9);
    rawref(cv::Rect(shift,0,width-shift,height)).copyTo(rawtarget(cv::Rect(0,0,width-shift,height)));
    if (kind=="random") rng.fill(rawtarget,cv::RNG::UNIFORM,0,256);
    else if (kind=="flat") { rawref.setTo(17+n);rawtarget.setTo(200-n); }
    else if (kind=="occlusion") rawtarget(cv::Rect(width/3,0,width/5,height)).setTo(0);
    else if (kind!="plane" && kind!="real" && kind!="extreme") throw std::runtime_error("Unknown case");
    if (!realref.empty()) { rawref=realref;rawtarget=realtarget; }
    auto begin=Clock::now();auto ref=prefilter(rawref),target=prefilter(rawtarget);
    if (kind=="extreme") { ref.setTo(0);target.setTo(62); }
    auto filtered=Clock::now();SadTiming timing;const uint64_t cpu_begin=cpu_ns();
    auto actual=tpu.run(ref,target,timing);auto tpu_end=Clock::now();
    const double tpu_cpu_ms=(cpu_ns()-cpu_begin)/1e6;
    auto expected=cpu_sad(ref,target,nd);auto cpu_end=Clock::now();
    int cost_errors=cv::countNonZero(actual.cost!=expected.cost);
    int disparity_errors=cv::countNonZero(actual.disparity!=expected.disparity);
    if (cost_errors || disparity_errors) {
      std::cerr << "FAIL cost=" << cost_errors << " disparity=" << disparity_errors << '\n';
      for (int y=7;y<height-7;++y) for (int x=nd-1+7;x<width-7;++x)
        if (actual.cost.at<uint16_t>(y,x)!=expected.cost.at<uint16_t>(y,x)) {
          std::cerr << "first mismatch " << x << ',' << y << " cost=" << actual.cost.at<uint16_t>(y,x)
                    << " expected=" << expected.cost.at<uint16_t>(y,x) << " d=" << int(actual.disparity.at<uint8_t>(y,x))
                    << " expected_d=" << int(expected.disparity.at<uint8_t>(y,x)) << '\n';goto reported;
        }
      reported: return 1;
    }
    auto bm_begin=Clock::now();cv::Mat bm_disparity;bm->compute(rawref,rawtarget,bm_disparity);auto bm_end=Clock::now();
    size_t plane_good=0,bm_valid=0,bm_agree=0,core=0,wta_positive=0;
    for (int y=7;y<height-7;++y) for (int x=nd-1+7;x<width-7;++x) {
      ++core;const int winner=actual.disparity.at<uint8_t>(y,x);
      wta_positive+=winner>0;
      plane_good+=winner==shift;
      auto q4=bm_disparity.at<int16_t>(y,x);
      if (q4>0) { ++bm_valid;bm_agree+=std::abs(winner-q4/16.)<=1.; }
    }
    const double plane_fraction=double(plane_good)/core;
    if (kind=="plane" && plane_fraction<.99) throw std::runtime_error("Known plane disparity error");
    out << n-2 << ',' << width << ',' << height << ',' << nd << ',' << kind << ',' << (tree?"tree":"direct") << ',' << ms(begin,filtered)
        << ',' << timing.upload_ms << ',' << timing.execute_ms << ',' << timing.download_ms
        << ',' << ms(filtered,tpu_end) << ',' << tpu_cpu_ms << ',' << ms(tpu_end,cpu_end) << ',' << ms(bm_begin,bm_end)
        << ',' << cost_errors << ',' << disparity_errors << ',' << plane_fraction << ',' << bm_valid
        << ',' << (bm_valid?double(bm_agree)/bm_valid:0) << ',' << wta_positive << '\n';out.flush();
  }
  std::cerr << "PASS exact integer SAD/WTA, init_ms=" << init_ms << "; StereoBM filters/subpixel not implemented on TPU\n";
  return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }

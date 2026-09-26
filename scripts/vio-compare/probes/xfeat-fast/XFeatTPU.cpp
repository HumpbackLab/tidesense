#include "XFeatTPU.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
struct Candidate { int x, y; float score; };
float cubic(float x) {
  x = std::abs(x);
  // PyTorch grid_sample bicubic uses alpha = -0.75.
  if (x < 1) return (1.25f*x - 2.25f)*x*x + 1;
  if (x < 2) return ((-0.75f*x + 3.75f)*x - 6)*x + 3;
  return 0;
}
float sample_cubic(const float *map, int width, int height, float x, float y) {
  const int ix = (int)std::floor(x), iy = (int)std::floor(y);
  float sum = 0;
  for (int dy=-1; dy<=2; ++dy) for (int dx=-1; dx<=2; ++dx) {
    int xx=ix+dx, yy=iy+dy;
    if (xx>=0 && xx<width && yy>=0 && yy<height)
      sum += map[yy*width+xx]*cubic(x-xx)*cubic(y-yy);
  }
  return sum;
}
float sample_linear(const float *map, int width, int height, float x, float y) {
  int ix=(int)std::floor(x), iy=(int)std::floor(y);
  float dx=x-ix, dy=y-iy, sum=0;
  for(int yy=0; yy<2; ++yy) for(int xx=0; xx<2; ++xx) {
    int sx=ix+xx, sy=iy+yy;
    if(sx>=0 && sx<width && sy>=0 && sy<height)
      sum += map[sy*width+sx]*(xx?dx:1-dx)*(yy?dy:1-dy);
  }
  return sum;
}
}

XFeatTPU::XFeatTPU(const char *path) {
  if (CVI_NN_RegisterModel(path, &model_) != 0 || !model_)
    throw std::runtime_error(std::string("XFeat model load failed: ")+path);
  if (CVI_NN_GetInputOutputTensors(model_, &inputs_, &input_count_, &outputs_, &output_count_) != 0 ||
      input_count_ != 1 || output_count_ != 3)
    throw std::runtime_error("XFeat model tensor layout mismatch");
  auto shape=CVI_NN_TensorShape(inputs_);
  if(shape.dim_size != 4 || shape.dim[0] != 1 || shape.dim[1] != 1 ||
     shape.dim[2] % 32 || shape.dim[3] % 32)
    throw std::runtime_error("XFeat input shape mismatch");
  height_=shape.dim[2]; width_=shape.dim[3];
}
XFeatTPU::~XFeatTPU() { if (model_) CVI_NN_CleanupModel(model_); }

void XFeatTPU::detect(const cv::Mat &image, const cv::Mat &mask, int top_k,
                      std::vector<cv::KeyPoint> &points, cv::Mat &descriptors) {
  const int W=width_, H=height_, CW=W/8, CH=H/8, CELLS=CW*CH;
  cv::Mat resized, f;
  cv::resize(image, resized, cv::Size(W,H), 0, 0, cv::INTER_LINEAR);
  resized.convertTo(f, CV_32F);
  cv::Scalar mean, stddev;
  cv::meanStdDev(f, mean, stddev);
  f = (f - mean[0]) / std::sqrt(stddev[0]*stddev[0] + 1e-5);
  auto *in = reinterpret_cast<float*>(CVI_NN_TensorPtr(inputs_));
  std::memcpy(in, f.ptr<float>(), sizeof(float)*W*H);
  if (CVI_NN_Forward(model_, inputs_, input_count_, outputs_, output_count_) != 0)
    throw std::runtime_error("XFeat TPU forward failed");
  const float *features=nullptr, *logits=nullptr, *reliability=nullptr;
  for (int i=0; i<output_count_; ++i) {
    const auto &o=outputs_[i];
    if (o.count == 64*CELLS) features=(const float*)CVI_NN_TensorPtr(outputs_+i);
    else if (o.count == 65*CELLS) logits=(const float*)CVI_NN_TensorPtr(outputs_+i);
    else if (o.count == CELLS) reliability=(const float*)CVI_NN_TensorPtr(outputs_+i);
  }
  if (!features || !logits || !reliability) throw std::runtime_error("XFeat output shapes mismatch");
  // Upstream XFeat normalizes the dense map before bicubic sampling.
  std::vector<float> unit_features(64*CELLS);
  for (int j=0;j<CELLS;++j) {
    float n=0;
    for(int k=0;k<64;++k) n+=features[k*CELLS+j]*features[k*CELLS+j];
    n=std::sqrt(n)+1e-12f;
    for(int k=0;k<64;++k) unit_features[k*CELLS+j]=features[k*CELLS+j]/n;
  }
  cv::Mat heat(H,W,CV_32F,cv::Scalar(0));
  for (int cy=0; cy<CH; ++cy) for (int cx=0; cx<CW; ++cx) {
    int j=cy*CW+cx;
    float peak=-1e30f;
    for (int k=0;k<65;++k) peak=std::max(peak,logits[k*CELLS+j]);
    float denom=0;
    for (int k=0;k<65;++k) denom+=std::exp(logits[k*CELLS+j]-peak);
    for (int k=0;k<64;++k)
      heat.at<float>(cy*8+k/8,cx*8+k%8)=std::exp(logits[k*CELLS+j]-peak)/denom;
  }
  std::vector<Candidate> candidates;
  for (int y=2;y<H-2;++y) for (int x=2;x<W-2;++x) {
    float p=heat.at<float>(y,x);
    if (p<=0.05f) continue;
    bool maximum=true;
    for (int dy=-2;dy<=2 && maximum;++dy) for(int dx=-2;dx<=2;++dx)
      if ((dy || dx) && heat.at<float>(y+dy,x+dx)>p) { maximum=false; break; }
    if (!maximum) continue;
    float ox=(float)x*image.cols/W, oy=(float)y*image.rows/H;
    int mx=std::min(std::max((int)std::round(ox),0),image.cols-1);
    int my=std::min(std::max((int)std::round(oy),0),image.rows-1);
    if (!mask.empty() && mask.at<uint8_t>(my,mx)>0) continue;
    float rx=(float)x*CW/(W-1)-0.5f, ry=(float)y*CH/(H-1)-0.5f;
    float r=sample_linear(reliability,CW,CH,rx,ry);
    candidates.push_back({x,y,p*r});
  }
  std::sort(candidates.begin(),candidates.end(),[](const Candidate&a,const Candidate&b){return a.score>b.score;});
  static int debug_calls=0;
  if (debug_calls++ < 4) fprintf(stderr,"XFEAT_DETECT candidates=%zu top_score=%.6f logits=%g,%g rel=%g feat=%g\n",
      candidates.size(), candidates.empty()?0:candidates[0].score, logits[0],logits[64*CELLS],reliability[0],features[0]);
  if ((int)candidates.size()>top_k) candidates.resize(top_k);
  descriptors=cv::Mat((int)candidates.size(),64,CV_32F);
  for (size_t i=0;i<candidates.size();++i) {
    const auto &c=candidates[i];
    float ox=(float)c.x*image.cols/W, oy=(float)c.y*image.rows/H;
    points.emplace_back(cv::Point2f(ox,oy),1.0f,-1,c.score);
    float *d=descriptors.ptr<float>((int)i);
    float fx=(float)c.x*CW/(W-1)-0.5f, fy=(float)c.y*CH/(H-1)-0.5f;
    float norm=0;
    for (int k=0;k<64;++k) {d[k]=sample_cubic(unit_features.data()+k*CELLS,CW,CH,fx,fy); norm+=d[k]*d[k];}
    norm=std::sqrt(norm)+1e-12f;
    for (int k=0;k<64;++k) d[k]/=norm;
  }
}

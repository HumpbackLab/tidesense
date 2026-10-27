// SPDX-License-Identifier: GPL-3.0-or-later
#include "tpu_sad.hpp"
#include <cviruntime_context.h>
#include <cvikernel/cvikernel.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>
#include <arm_neon.h>

namespace {
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b) { return std::chrono::duration<double,std::milli>(b-a).count(); }
void check(int rc,const char* s) { if (rc) throw std::runtime_error(std::string(s)+": "+std::to_string(rc)); }
struct Kernel {
  cvk_context_t* c;
  std::vector<cvk_tl_t*> tensors;
  explicit Kernel(CVI_RT_HANDLE rt) : c(static_cast<cvk_context_t*>(CVI_RT_RegisterKernel(rt,2*1024*1024))) {
    if (!c) throw std::runtime_error("RegisterKernel failed");
    if (c->info.npu_num!=8 || c->info.lmem_size!=32768) {
      CVI_RT_UnRegisterKernel(c);c=nullptr;
      throw std::runtime_error("Expected CV181x 8-NPU/32KiB backend");
    }
  }
  ~Kernel() {
    for (auto i=tensors.rbegin();i!=tensors.rend();++i) c->ops->lmem_free_tensor(c,*i);
    CVI_RT_UnRegisterKernel(c);
  }
  cvk_tl_t* alloc(int channels,int h,int w) {
    auto t=c->ops->lmem_alloc_tensor(c,{1,unsigned(channels),unsigned(h),unsigned(w)},CVK_FMT_I8,1);
    if (!t) throw std::runtime_error("Tile exceeds local memory; reduce tile size");
    tensors.push_back(t);return t;
  }
  void fill(const cvk_tl_t* t,uint8_t value) {
    cvk_tdma_g2l_tensor_fill_constant_param_t p{};p.dst=t;p.constant=value;
    c->ops->tdma_g2l_tensor_fill_constant(c,&p);
  }
  void copy(const cvk_tl_t* src,const cvk_tl_t* dst) {
    cvk_tiu_copy_param_t p{};p.src=src;p.dst=dst;c->ops->tiu_copy(c,&p);
  }
};
cvk_tl_t view(const cvk_tl_t* tensor,int h,int w,int row=0,int column=0) {
  auto t=*tensor;t.shape.h=h;t.shape.w=w;
  t.start_address+=row*t.stride.h+column*t.stride.w;
  return t;
}
struct Pair { cvk_tl_t *low,*high; };
struct PairView { cvk_tl_t low,high;Pair ptrs() { return {&low,&high}; } };
PairView slice(Pair p,int h,int w,int row=0,int col=0) {
  return {view(p.low,h,w,row,col),view(p.high,h,w,row,col)};
}
Pair pair(Kernel& k,int c,int h,int w) { return {k.alloc(c,h,w),k.alloc(c,h,w)}; }
void copy_pair(Kernel& k,Pair a,Pair b) { k.copy(a.low,b.low);k.copy(a.high,b.high); }
void add(Kernel& k,Pair a,Pair b,Pair out) {
  cvk_tiu_add_param_t p{};p.a_low=a.low;p.a_high=a.high;p.b.low=b.low;p.b.high=b.high;
  p.res_low=out.low;p.res_high=out.high;k.c->ops->tiu_add(k.c,&p);
}
void add_views(Kernel& k,PairView a,PairView b,Pair out) { add(k,a.ptrs(),b.ptrs(),out); }
// Exact recurrence S3=2+1, S7=3+3+1, S15=7+7+1: six adds per axis.
void sum15(Kernel& k,Pair in,Pair level3,Pair level7,Pair out,int h,int w,bool vertical) {
  const int h3=h+(vertical?12:0),w3=w+(vertical?0:12);
  const int h7=h+(vertical?8:0),w7=w+(vertical?0:8);
  auto shifted=[&](Pair p,int hh,int ww,int offset) { return slice(p,hh,ww,vertical?offset:0,vertical?0:offset); };
  add_views(k,shifted(in,h3,w3,0),shifted(in,h3,w3,1),level3);
  add_views(k,slice(level3,h3,w3),shifted(in,h3,w3,2),level3);
  add_views(k,shifted(level3,h7,w7,0),shifted(level3,h7,w7,3),level7);
  add_views(k,slice(level7,h7,w7),shifted(in,h7,w7,6),level7);
  add_views(k,shifted(level7,h,w,0),shifted(level7,h,w,7),out);
  add_views(k,slice(out,h,w),shifted(in,h,w,14),out);
}
void subtract(Kernel& k,Pair a,Pair b,Pair out) {
  cvk_tiu_sub_param_t p{};p.a_low=a.low;p.a_high=a.high;p.b_low=b.low;p.b_high=b.high;
  p.res_low=out.low;p.res_high=out.high;k.c->ops->tiu_sub(k.c,&p);
}
void xor16(Kernel& k,Pair a,Pair b,Pair out) {
  cvk_tiu_xor_int16_param_t p{};p.a_low=a.low;p.a_high=a.high;p.b_low=b.low;p.b_high=b.high;
  p.res_low=out.low;p.res_high=out.high;k.c->ops->tiu_xor_int16(k.c,&p);
}
void and16(Kernel& k,Pair a,Pair b,Pair out) {
  cvk_tiu_and_int16_param_t p{};p.a_low=a.low;p.a_high=a.high;p.b_low=b.low;p.b_high=b.high;
  p.res_low=out.low;p.res_high=out.high;k.c->ops->tiu_and_int16(k.c,&p);
}
void xor8(Kernel& k,cvk_tl_t* a,cvk_tl_t* b,cvk_tl_t* out) {
  cvk_tiu_xor_int8_param_t p{};p.a=a;p.b=b;p.res=out;k.c->ops->tiu_xor_int8(k.c,&p);
}
void and8(Kernel& k,cvk_tl_t* a,cvk_tl_t* b,cvk_tl_t* out) {
  cvk_tiu_and_int8_param_t p{};p.a=a;p.b=b;p.res=out;k.c->ops->tiu_and_int8(k.c,&p);
}
}

struct TpuSad::Impl {
  CVI_RT_HANDLE rt=nullptr;
  CVI_RT_MEM input[2]={},output=nullptr;
  int width,height,nd,tw,th;
  bool tree,quality;
  using Key=std::tuple<int,int,int>;
  std::map<Key,CVI_RT_MEM> programs;
  struct Tile { int x,y,c,h,w; CVI_RT_MEM program; };
  std::vector<Tile> tiles;
  Impl(int w,int h,int d,int tilew,int tileh,bool use_tree,bool use_quality):width(w),height(h),nd(d),tw(tilew),th(tileh),tree(use_tree),quality(use_quality) {
    if (nd<1 || nd>64 || width<=nd+14 || height<=14 || tw<1 || th<1)
      throw std::runtime_error("Invalid TPU SAD geometry (D <=64)");
    check(CVI_RT_Init(&rt),"RT init");
    if (!rt) throw std::runtime_error("RT init returned no handle");
    try {
      for (auto& m:input) { m=CVI_RT_MemAlloc(rt,size_t(w)*h);if (!m) throw std::runtime_error("Input allocation failed"); }
      output=CVI_RT_MemAlloc(rt,size_t(w)*h*(quality?9:3));
      if (!output) throw std::runtime_error("Output allocation failed");
      std::memset(CVI_RT_MemGetVAddr(output),0,size_t(w)*h*(quality?9:3));check(CVI_RT_MemFlush(rt,output),"output clear");
      for (int y=7;y<height-7;) {
        const int rows=std::min(th,height-7-y), channels=std::min(8,(height-7-y)/rows);
        for (int x=nd-1+7;x<width-7;x+=tw) {
          const int columns=std::min(tw,width-7-x);Key key{channels,rows,columns};
          if (!programs.count(key)) programs.emplace(key,compile(channels,rows,columns));
          tiles.push_back({x,y,channels,rows,columns,programs.at(key)});
        }
        y+=channels*rows;
      }
      std::cerr << "TPU SAD: tiles=" << tiles.size() << " programs=" << programs.size()
                << " tile=" << tw << 'x' << th << " D=" << nd << " aggregation=" << (tree?"tree":"direct") << '\n';
    } catch (...) { cleanup();throw; }
  }
  void cleanup() {
    for (auto& p:programs) CVI_RT_MemFree(rt,p.second);
    programs.clear();
    if (output) { CVI_RT_MemFree(rt,output);output=nullptr; }
    for (auto& m:input) if (m) { CVI_RT_MemFree(rt,m);m=nullptr; }
    if (rt) { CVI_RT_DeInit(rt);rt=nullptr; }
  }
  ~Impl() { cleanup(); }
  CVI_RT_MEM compile(int c,int h,int w) {
    Kernel k(rt);
    auto left=k.alloc(c,h+14,w+14),right=k.alloc(c,h+14,w+14+nd-1);
    auto minimum=k.alloc(c,h+14,w+14),difference=k.alloc(c,h+14,w+14);
    // The high zero plane must have the same stride as the difference input.
    auto zero=k.alloc(c,h+14,w+14);
    auto horizontal=pair(k,c,h+14,w+(tree?12:0));
    Pair horizontal7{},vertical3{},vertical7{};
    if (tree) {
      horizontal7=pair(k,c,h+14,w+8);vertical3=pair(k,c,h+12,w);vertical7=pair(k,c,h+8,w);
    }
    auto cost=pair(k,c,h,w),best=pair(k,c,h,w);
    auto delta=pair(k,c,h,w),mask=pair(k,c,h,w);
    auto shift=k.alloc(c,h,w),disp=k.alloc(c,h,w),dconstant=k.alloc(c,h,w),tmpdisp=k.alloc(c,h,w);
    Pair previous{},minus{},plus{},previous_mask{},prefix{},competitor{},far{};
    cvk_tl_t *outzero=nullptr,*distance=nullptr,*boolean=nullptr;
    if (quality) {
      previous=pair(k,c,h,w);minus=pair(k,c,h,w);plus=pair(k,c,h,w);
      previous_mask=pair(k,c,h,w);prefix=pair(k,c,h,w);
      competitor=pair(k,c,h,w);far=pair(k,c,h,w);
      outzero=k.alloc(c,h,w);distance=k.alloc(c,h,w);boolean=k.alloc(c,h,w);
    }
    auto select=[&](Pair mask_,Pair value,Pair dest) {
      xor16(k,dest,value,delta);and16(k,delta,mask_,delta);xor16(k,dest,delta,dest);
    };
    auto less=[&](Pair a,Pair b,Pair out) {
      subtract(k,a,b,delta);
      cvk_tiu_arith_shift_param_t sh{};sh.a_low=delta.low;sh.a_high=delta.high;
      sh.res_low=out.low;sh.res_high=out.high;sh.bits=shift;k.c->ops->tiu_arith_shift(k.c,&sh);
    };
    auto dma=[&](cvk_tl_t* t,int base,bool load,uint64_t offset=0) {
      cvk_tg_t global{};global.base_reg_index=base;global.start_address=offset;global.fmt=CVK_FMT_I8;
      global.shape={1,unsigned(c),t->shape.h,t->shape.w};
      global.stride={unsigned(width*height),unsigned(h*width),unsigned(width),1};
      if (load) { cvk_tdma_g2l_tensor_copy_param_t p{};p.src=&global;p.dst=t;k.c->ops->tdma_g2l_tensor_copy(k.c,&p); }
      else { cvk_tdma_l2g_tensor_copy_param_t p{};p.src=t;p.dst=&global;k.c->ops->tdma_l2g_tensor_copy(k.c,&p); }
    };
    dma(left,2,true);dma(right,3,true);
    k.fill(zero,0);k.fill(best.low,255);k.fill(best.high,127);k.fill(shift,uint8_t(-15));k.fill(disp,0);
    if (quality) {
      for (auto p:{previous,minus,plus,previous_mask}) { k.fill(p.low,0);k.fill(p.high,0); }
      for (auto p:{prefix,competitor}) { k.fill(p.low,255);k.fill(p.high,127); }
      k.fill(outzero,0);
    }
    for (int d=0;d<nd;++d) {
      auto shifted=view(right,h+14,w+14,0,nd-1-d);
      cvk_tiu_min_param_t pn{};pn.a=left;pn.b=&shifted;pn.min=minimum;k.c->ops->tiu_min(k.c,&pn);
      cvk_tiu_max_param_t px{};px.a=left;px.b=&shifted;px.max=difference;k.c->ops->tiu_max(k.c,&px);
      cvk_tiu_sub_param_t ps{};ps.a_low=difference;ps.a_high=zero;ps.b_low=minimum;ps.b_high=zero;ps.res_low=difference;
      k.c->ops->tiu_sub(k.c,&ps); // Difference <=62, exact signed byte output.
      if (tree) {
        auto summed=slice(horizontal,h+14,w);
        sum15(k,{difference,zero},horizontal,horizontal7,summed.ptrs(),h+14,w,false);
        sum15(k,summed.ptrs(),vertical3,vertical7,cost,h,w,true);
      } else {
        auto first=view(difference,h+14,w);
        k.copy(&first,horizontal.low);k.fill(horizontal.high,0);
        for (int x=1;x<15;++x) {
          auto lo=view(difference,h+14,w,0,x),hi=view(zero,h+14,w,0,x);
          add(k,horizontal,{&lo,&hi},horizontal);
        }
        auto hlo=view(horizontal.low,h,w),hhi=view(horizontal.high,h,w);
        copy_pair(k,{&hlo,&hhi},cost);
        for (int y=1;y<15;++y) {
          auto lo=view(horizontal.low,h,w,y),hi=view(horizontal.high,h,w,y);
          add(k,cost,{&lo,&hi},cost);
        }
      }
      auto zout=view(zero,h,w);
      cvk_tiu_max_param_t pc{};pc.a=&zout;pc.b_is_const=1;pc.b_const.val=d;pc.b_const.is_signed=1;pc.max=dconstant;
      k.c->ops->tiu_max(k.c,&pc);
      if (quality) {
        select(previous_mask,cost,plus);
        cvk_tiu_sub_param_t sub{};sub.a_low=dconstant;sub.a_high=outzero;
        sub.b_low=disp;sub.b_high=outzero;sub.res_low=distance;k.c->ops->tiu_sub(k.c,&sub);
        cvk_tiu_ge_param_t ge{};ge.a=distance;ge.b_is_const=1;
        ge.b_const.val=2;ge.b_const.is_signed=1;ge.ge=boolean;k.c->ops->tiu_ge(k.c,&ge);
        cvk_tiu_mul_param_t mul{};mul.a=boolean;mul.b_is_const=1;
        mul.b_const.val=-1;mul.b_const.is_signed=1;mul.res_low=far.low;k.c->ops->tiu_mul(k.c,&mul);
        k.copy(far.low,far.high);
        less(cost,competitor,mask);and16(k,mask,far,mask);select(mask,cost,competitor);
      }
      // Arithmetic sign mask selects strictly lower cost; lowest disparity wins ties.
      less(cost,best,mask);
      if (quality) {
        select(mask,previous,minus);select(mask,prefix,competitor);
        copy_pair(k,best,prefix); // Prefix minimum through d-1 for the next iteration.
      }
      select(mask,cost,best);
      xor8(k,disp,dconstant,tmpdisp);and8(k,tmpdisp,mask.low,tmpdisp);xor8(k,disp,tmpdisp,disp);
      if (quality) { copy_pair(k,cost,previous);copy_pair(k,mask,previous_mask); }
    }
    dma(best.low,4,false);dma(best.high,4,false,size_t(width)*height);dma(disp,4,false,size_t(width)*height*2);
    if (quality) {
      int plane=3;
      for (auto p:{minus,plus,competitor}) {
        dma(p.low,4,false,size_t(width)*height*plane++);
        dma(p.high,4,false,size_t(width)*height*plane++);
      }
    }
    uint32_t size=0;auto bytes=k.c->ops->acquire_cmdbuf(k.c,&size);
    if (!bytes || !size) throw std::runtime_error("Empty TPU command buffer");
    CVI_RT_MEM program=nullptr;
    check(CVI_RT_LoadCmdbuf(rt,bytes,size,0,0,false,&program),"LoadCmdbuf");
    if (!program) throw std::runtime_error("No loaded TPU program");
    return program;
  }
  SadResult run(const cv::Mat& reference,const cv::Mat& target,SadTiming& timing) {
    if (reference.size()!=cv::Size(width,height) || target.size()!=reference.size() ||
        reference.type()!=CV_8UC1 || target.type()!=CV_8UC1)
      throw std::runtime_error("Expected matching mono8 inputs");
    double lo,hi;cv::minMaxLoc(reference,&lo,&hi);if (hi>62) throw std::runtime_error("Reference exceeds prefilter cap");
    cv::minMaxLoc(target,&lo,&hi);if (hi>62) throw std::runtime_error("Target exceeds prefilter cap");
    const auto start=Clock::now();int side=0;
    for (const auto* image:{&reference,&target}) {
      auto dst=CVI_RT_MemGetVAddr(input[side]);
      for (int y=0;y<height;++y) std::memcpy(dst+size_t(y)*width,image->ptr(y),width);
      check(CVI_RT_MemFlush(rt,input[side]),"input flush");++side;
    }
    const auto uploaded=Clock::now();
    for (const auto& tile:tiles) {
      CVI_RT_ARRAYBASE base{};
      base.gaddr_base2=CVI_RT_MemGetPAddr(input[0])+size_t(tile.y-7)*width+tile.x-7;
      base.gaddr_base3=CVI_RT_MemGetPAddr(input[1])+size_t(tile.y-7)*width+tile.x-7-(nd-1);
      base.gaddr_base4=CVI_RT_MemGetPAddr(output)+size_t(tile.y)*width+tile.x;
      check(CVI_RT_RunCmdbufEx(rt,tile.program,&base),"RunCmdbufEx");
    }
    const auto executed=Clock::now();
    check(CVI_RT_MemInvld(rt,output),"output invalidate");
    auto bytes=CVI_RT_MemGetVAddr(output);const size_t pixels=size_t(width)*height;
    SadResult result;result.cost=cv::Mat(height,width,CV_16UC1);result.disparity=cv::Mat(height,width,CV_8UC1);
    size_t i=0;auto z=result.cost.ptr<uint16_t>();
    for (;i+8<=pixels;i+=8)
      vst1q_u16(z+i,vorrq_u16(vmovl_u8(vld1_u8(bytes+i)),vshll_n_u8(vld1_u8(bytes+i+pixels),8)));
    for (;i<pixels;++i) z[i]=bytes[i]|uint16_t(bytes[i+pixels])<<8;
    std::memcpy(result.disparity.data,bytes+pixels*2,pixels);
    if (quality) {
      int plane=3;
      for (auto m:{&result.minus,&result.plus,&result.competitor}) {
        m->create(height,width,CV_16U);auto dst=m->ptr<uint16_t>();
        for (size_t j=0;j<pixels;j++) dst[j]=bytes[j+pixels*plane]|uint16_t(bytes[j+pixels*(plane+1)])<<8;
        plane+=2;
      }
    }
    const auto end=Clock::now();
    timing={ms(start,uploaded),ms(uploaded,executed),ms(executed,end),int(tiles.size())};
    return result;
  }
};
TpuSad::TpuSad(int w,int h,int d,int tw,int th,bool tree,bool quality):impl_(new Impl(w,h,d,tw,th,tree,quality)) {}
TpuSad::~TpuSad()=default;
SadResult TpuSad::run(const cv::Mat& a,const cv::Mat& b,SadTiming& t) { return impl_->run(a,b,t); }

struct TpuStereoDownsample::Impl {
  CVI_RT_HANDLE rt=nullptr;
  CVI_RT_MEM input=nullptr,output=nullptr;
  struct Tile { int y,c,h;CVI_RT_MEM program; };
  std::vector<Tile> tiles;
  Impl() {
    check(CVI_RT_Init(&rt),"downsample init");
    try {
      input=CVI_RT_MemAlloc(rt,2*640*400);output=CVI_RT_MemAlloc(rt,2*320*200);
      if (!input || !output) throw std::runtime_error("Downsample allocation failed");
      for (int y=0;y<200;) {
        int h=std::min(16,200-y),c=std::min(8,(200-y)/h);
        Kernel k(rt);
        auto in=k.alloc(c,h*2,640),out=k.alloc(c,h,320);
        in->fmt=CVK_FMT_U8;out->fmt=CVK_FMT_U8;
        cvk_tg_t gi{};gi.base_reg_index=2;gi.fmt=CVK_FMT_U8;gi.shape={1,unsigned(c),unsigned(h*2),640};
        gi.stride={640*400,unsigned(h*2*640),640,1};
        cvk_tdma_g2l_tensor_copy_param_t load{};load.src=&gi;load.dst=in;k.c->ops->tdma_g2l_tensor_copy(k.c,&load);
        cvk_tiu_average_pooling_param_t pool{};pool.ifmap=in;pool.ofmap=out;pool.kh=pool.kw=2;
        pool.stride_h=pool.stride_w=2;pool.avg_pooling_const=1;pool.rshift_bits=2;
        k.c->ops->tiu_average_pooling(k.c,&pool);
        cvk_tg_t go{};go.base_reg_index=3;go.fmt=CVK_FMT_U8;go.shape={1,unsigned(c),unsigned(h),320};
        go.stride={320*200,unsigned(h*320),320,1};
        cvk_tdma_l2g_tensor_copy_param_t store{};store.src=out;store.dst=&go;k.c->ops->tdma_l2g_tensor_copy(k.c,&store);
        uint32_t size=0;auto bytes=k.c->ops->acquire_cmdbuf(k.c,&size);CVI_RT_MEM program=nullptr;
        check(CVI_RT_LoadCmdbuf(rt,bytes,size,0,0,false,&program),"downsample load");
        tiles.push_back({y,c,h,program});y+=h*c;
      }
    } catch (...) { cleanup();throw; }
  }
  void cleanup() {
    for (auto t:tiles) CVI_RT_MemFree(rt,t.program);
    tiles.clear();
    if (output) { CVI_RT_MemFree(rt,output);output=nullptr; }
    if (input) { CVI_RT_MemFree(rt,input);input=nullptr; }
    if (rt) { CVI_RT_DeInit(rt);rt=nullptr; }
  }
  ~Impl() { cleanup(); }
  std::array<cv::Mat,2> run(const cv::Mat& left,const cv::Mat& right,SadTiming& timing) {
    auto start=Clock::now();
    int side=0;auto in=CVI_RT_MemGetVAddr(input);
    for (auto image:{&left,&right}) {
      if (image->size()!=cv::Size(640,400) || image->type()!=CV_8UC1) throw std::runtime_error("Downsample expects 640x400 mono8");
      for (int y=0;y<400;++y) std::memcpy(in+side*640*400+y*640,image->ptr(y),640);
      ++side;
    }
    check(CVI_RT_MemFlush(rt,input),"downsample flush");auto uploaded=Clock::now();
    for (side=0;side<2;++side) for (auto t:tiles) {
      CVI_RT_ARRAYBASE base{};base.gaddr_base2=CVI_RT_MemGetPAddr(input)+side*640*400+t.y*2*640;
      base.gaddr_base3=CVI_RT_MemGetPAddr(output)+side*320*200+t.y*320;
      check(CVI_RT_RunCmdbufEx(rt,t.program,&base),"downsample execute");
    }
    auto executed=Clock::now();check(CVI_RT_MemInvld(rt,output),"downsample invalidate");
    auto out=CVI_RT_MemGetVAddr(output);
    std::array<cv::Mat,2> result{{cv::Mat(200,320,CV_8U),cv::Mat(200,320,CV_8U)}};
    for (side=0;side<2;++side) std::memcpy(result[side].data,out+side*320*200,320*200);
    timing={ms(start,uploaded),ms(uploaded,executed),ms(executed,Clock::now()),int(tiles.size()*2)};
    return result;
  }
};
TpuStereoDownsample::TpuStereoDownsample():impl_(new Impl) {}
TpuStereoDownsample::~TpuStereoDownsample()=default;
std::array<cv::Mat,2> TpuStereoDownsample::run(const cv::Mat& a,const cv::Mat& b,SadTiming& t) { return impl_->run(a,b,t); }

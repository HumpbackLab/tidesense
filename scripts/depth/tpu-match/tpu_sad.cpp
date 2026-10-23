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
  bool tree;
  using Key=std::tuple<int,int,int>;
  std::map<Key,CVI_RT_MEM> programs;
  struct Tile { int x,y,c,h,w; CVI_RT_MEM program; };
  std::vector<Tile> tiles;
  Impl(int w,int h,int d,int tilew,int tileh,bool use_tree):width(w),height(h),nd(d),tw(tilew),th(tileh),tree(use_tree) {
    if (nd<1 || nd>64 || width<=nd+14 || height<=14 || tw<1 || th<1)
      throw std::runtime_error("Invalid TPU SAD geometry (D <=64)");
    check(CVI_RT_Init(&rt),"RT init");
    if (!rt) throw std::runtime_error("RT init returned no handle");
    try {
      for (auto& m:input) { m=CVI_RT_MemAlloc(rt,size_t(w)*h);if (!m) throw std::runtime_error("Input allocation failed"); }
      output=CVI_RT_MemAlloc(rt,size_t(w)*h*3);
      if (!output) throw std::runtime_error("Output allocation failed");
      std::memset(CVI_RT_MemGetVAddr(output),0,size_t(w)*h*3);check(CVI_RT_MemFlush(rt,output),"output clear");
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
    auto dma=[&](cvk_tl_t* t,int base,bool load,uint64_t offset=0) {
      cvk_tg_t global{};global.base_reg_index=base;global.start_address=offset;global.fmt=CVK_FMT_I8;
      global.shape={1,unsigned(c),t->shape.h,t->shape.w};
      global.stride={unsigned(width*height),unsigned(h*width),unsigned(width),1};
      if (load) { cvk_tdma_g2l_tensor_copy_param_t p{};p.src=&global;p.dst=t;k.c->ops->tdma_g2l_tensor_copy(k.c,&p); }
      else { cvk_tdma_l2g_tensor_copy_param_t p{};p.src=t;p.dst=&global;k.c->ops->tdma_l2g_tensor_copy(k.c,&p); }
    };
    dma(left,2,true);dma(right,3,true);
    k.fill(zero,0);k.fill(best.low,255);k.fill(best.high,127);k.fill(shift,uint8_t(-15));k.fill(disp,0);
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
      // Exact 16-bit SAD <=62*225=13950. Arithmetic sign mask selects strictly lower cost.
      subtract(k,cost,best,delta);
      cvk_tiu_arith_shift_param_t sh{};sh.a_low=delta.low;sh.a_high=delta.high;
      sh.res_low=mask.low;sh.res_high=mask.high;sh.bits=shift;k.c->ops->tiu_arith_shift(k.c,&sh);
      xor16(k,best,cost,delta);and16(k,delta,mask,delta);xor16(k,best,delta,best);
      auto zout=view(zero,h,w);
      cvk_tiu_max_param_t pc{};pc.a=&zout;pc.b_is_const=1;pc.b_const.val=d;pc.b_const.is_signed=1;pc.max=dconstant;
      k.c->ops->tiu_max(k.c,&pc);
      xor8(k,disp,dconstant,tmpdisp);and8(k,tmpdisp,mask.low,tmpdisp);xor8(k,disp,tmpdisp,disp);
    }
    dma(best.low,4,false);dma(best.high,4,false,size_t(width)*height);dma(disp,4,false,size_t(width)*height*2);
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
    SadResult result{cv::Mat(height,width,CV_16UC1),cv::Mat(height,width,CV_8UC1)};
    size_t i=0;auto z=result.cost.ptr<uint16_t>();
    for (;i+8<=pixels;i+=8)
      vst1q_u16(z+i,vorrq_u16(vmovl_u8(vld1_u8(bytes+i)),vshll_n_u8(vld1_u8(bytes+i+pixels),8)));
    for (;i<pixels;++i) z[i]=bytes[i]|uint16_t(bytes[i+pixels])<<8;
    std::memcpy(result.disparity.data,bytes+pixels*2,pixels);
    const auto end=Clock::now();
    timing={ms(start,uploaded),ms(uploaded,executed),ms(executed,end),int(tiles.size())};
    return result;
  }
};
TpuSad::TpuSad(int w,int h,int d,int tw,int th,bool tree):impl_(new Impl(w,h,d,tw,th,tree)) {}
TpuSad::~TpuSad()=default;
SadResult TpuSad::run(const cv::Mat& a,const cv::Mat& b,SadTiming& t) { return impl_->run(a,b,t); }

#pragma once
#include "openvins_backend_config.h"
#include <cstdlib>
#include <cstring>
namespace ov_backend_probe {
#if OPENVINS_EKF_PACKED_F64
inline bool packed_f64(){static const bool value=[](){const char *p=std::getenv("OPENVINS_EKF_PACKED_F64");return p && !std::strcmp(p,"1");}();return value;}
#endif
}
#if OPENVINS_BACKEND_DETAIL
#include <chrono>
#include <cstdio>
#include <stdexcept>
namespace ov_backend_probe {
inline double now(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct Output {
 FILE *file=nullptr;double first=0,last=1e100;
 Output(){
  const char *path=std::getenv("OPENVINS_BACKEND_DETAIL_FILE");if(!path)return;
  file=std::fopen(path,"w");if(!file)throw std::runtime_error("Cannot open backend detail log");
  if(const char *p=std::getenv("OPENVINS_BACKEND_DETAIL_FIRST"))first=std::strtod(p,nullptr);
  if(const char *p=std::getenv("OPENVINS_BACKEND_DETAIL_LAST"))last=std::strtod(p,nullptr);
  std::fprintf(file,"timestamp,function,stage,wall_ms,a,b,c\n");
 }
 ~Output(){if(file)std::fclose(file);}
};
inline Output& output(){static Output value;return value;}
struct Probe {
 double stamp,start=0,last=0;const char *function;int a,b,c;bool active;
 Probe(double ts,const char *fn,int aa,int bb,int cc):stamp(ts),function(fn),a(aa),b(bb),c(cc){
  auto &o=output();active=o.file && ts>=o.first && ts<=o.last;if(active)start=last=now();
 }
 void mark(const char *stage){if(!active)return;double end=now();std::fprintf(output().file,"%.9f,%s,%s,%.6f,%d,%d,%d\n",stamp,function,stage,end-last,a,b,c);last=now();}
 ~Probe(){if(active)std::fprintf(output().file,"%.9f,%s,total,%.6f,%d,%d,%d\n",stamp,function,now()-start,a,b,c);}
};
}
#define OV_BACKEND_BEGIN(name,...) ov_backend_probe::Probe name(__VA_ARGS__)
#define OV_BACKEND_MARK(name,stage) name.mark(stage)
#else
#define OV_BACKEND_BEGIN(name,...)
#define OV_BACKEND_MARK(name,stage)
#endif

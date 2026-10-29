// Independent microbenchmark: existing pyramids, 16/32 complete point tracks.
#include "window_lk.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <time.h>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#ifdef CPU_WINDOW_LK_DEVICE
#include "cvi_sys.h"
#include "../c906-tpu/lk_shared.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
extern "C" int lk_tpu_make_jobs(unsigned char*,uint64_t);
#endif
static double wall(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static double cpu(){timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec*1000.+t.tv_nsec*1e-6;}
static std::vector<std::string> split(const std::string& line){std::stringstream s(line);std::string x;std::vector<std::string> r;while(std::getline(s,x,','))r.push_back(x);return r;}
static void full_track(const std::vector<cv::Mat>& prev,const std::vector<cv::Mat>& next,const std::vector<cv::Point2f>& points,std::vector<cv::Point2f>& output,std::vector<unsigned char>& valid){
 output=points;cv::calcOpticalFlowPyrLK(prev,next,points,output,valid,cv::noArray(),cv::Size(16,15),int(prev.size()/2)-1,cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
}
static void verify(const window_lk::Workspace& work,const std::vector<cv::Mat>& full,const std::vector<cv::Point2f>& points,
 const std::vector<cv::Point2f>& output,const std::vector<unsigned char>& valid,const std::vector<cv::Point2f>& ref,const std::vector<unsigned char>& status){
 if(valid!=status || output!=ref)throw std::runtime_error("Sparse window LK differs from full-gradient OpenCV");
 for(size_t l=0;l<work.derivatives.size();++l)for(const auto& p:points){
  int x=cvFloor(p.x/(1u<<l)-7.5f),y=cvFloor(p.y/(1u<<l)-7.f);const auto& d=work.derivatives[l];const auto& r=full[2*l+1];
  if(x < -16 || x>=d.cols || y < -15 || y>=d.rows)continue;
  for(int yy=y;yy<=y+15;++yy){
   auto a=reinterpret_cast<const short*>(d.data+ptrdiff_t(yy)*ptrdiff_t(d.step))+2*x;
   auto b=reinterpret_cast<const short*>(r.data+ptrdiff_t(yy)*ptrdiff_t(r.step))+2*x;
   if(std::memcmp(a,b,17*2*sizeof(short)))throw std::runtime_error("Sparse Scharr differs from OpenCV");
  }
 }
}
static void selftest(){
 unsigned total=0;
 for(int height:{400,480})for(int seed=0;seed<3;++seed){
  cv::Mat a(height,640,CV_8UC1),b;cv::RNG rng(100+seed);rng.fill(a,cv::RNG::UNIFORM,0,256);
  if(seed==1)cv::GaussianBlur(a,a,cv::Size(5,5),1.);
  if(seed==2)a.setTo(80);
  cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,2.35,0,1,-1.7);
  cv::warpAffine(a,b,transform,a.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
  std::vector<cv::Mat> gray,next,full;cv::buildOpticalFlowPyramid(a,gray,cv::Size(16,15),4,false);cv::buildOpticalFlowPyramid(b,next,cv::Size(16,15),4,false);cv::buildOpticalFlowPyramid(a,full,cv::Size(16,15),4,true);
  window_lk::Workspace work(gray);std::vector<cv::Point2f> points;
  for(int i=0;i<64;++i)points.emplace_back(rng.uniform(1.f,639.f),rng.uniform(1.f,float(height-1)));
  for(auto p:std::vector<cv::Point2f>{{0,0},{639,float(height-1)},{-.25f,-.5f},{-8.5f,-7.75f},{640.2f,200},{300,float(height)+.3f},{-1000,10},{10,-1000},{645,float(height)+5}})points.push_back(p);
  std::vector<cv::Point2f> out,ref;std::vector<unsigned char> v,s;
  // Poison all untouched derivatives. Correct output must not depend on them.
  for(auto& im:work.derivatives)im.setTo(cv::Scalar(12345,-23456));
  work.prepare(gray,points);work.track(next,points,out,v);full_track(full,next,points,ref,s);
  verify(work,full,points,out,v,ref,s);total+=points.size();
 }
 std::cout<<"SELFTEST PASS cases=6 points="<<total<<" exact_gradients=1 exact_status=1 exact_coordinates=1\n";
}
#ifdef CPU_WINDOW_LK_DEVICE
struct Shared {
 CVI_U64 phys=0;void *memory=nullptr;int fd=-1;unsigned bytes=0;
 Shared(bool tpu){
  if(CVI_SYS_IonAlloc_Cached(&phys,&memory,"cpu_window_lk",LK_TPU_BYTES) || !memory || phys>0x8ff40000u-LK_TPU_BYTES)throw std::runtime_error("ION allocation failed");
  std::memset(memory,0,LK_TPU_BYTES);
  auto& m=meta();m.version=LK_TPU_VERSION;m.batch=16;m.max_iterations=30;
  if(tpu){if(lk_tpu_make_jobs(base(),phys))throw std::runtime_error("Job generation failed");fd=open("/dev/c906-tpu-probe",O_RDWR);if(fd<0)throw std::runtime_error("TPU bridge unavailable");}
 }
 ~Shared(){if(fd>=0)close(fd);if(memory)CVI_SYS_IonFree(phys,memory);}
 unsigned char *base(){return static_cast<unsigned char*>(memory);}
 lk_tpu_control& meta(){return *reinterpret_cast<lk_tpu_control*>(base()+LK_TPU_META_OFF);}
 std::pair<std::vector<cv::Mat>,std::vector<cv::Mat>> pack(const std::vector<cv::Mat>& a,const std::vector<cv::Mat>& b){
  auto& m=meta();m.levels=a.size();unsigned off=LK_TPU_PLANES_OFF;std::vector<cv::Mat> views[2];
  for(unsigned l=0;l<a.size();++l){auto& level=m.level[l];level.width=a[l].cols;level.height=a[l].rows;level.row_bytes=level.width+36;
   for(unsigned p=0;p<2;++p){off=(off+63)&~63u;unsigned size=(level.height+36)*level.row_bytes;if(off+size>LK_TPU_BYTES)throw std::runtime_error("Shared pyramid overflow");
    level.planes[p]=off;cv::Mat full(level.height+36,level.row_bytes,CV_8UC1,base()+off);
    cv::copyMakeBorder(p?b[l]:a[l],full,18,18,18,18,cv::BORDER_REFLECT_101|cv::BORDER_ISOLATED);
    views[p].push_back(full(cv::Rect(18,18,level.width,level.height)));off+=size;
   }
  }
  bytes=off-LK_TPU_PLANES_OFF;
  if(CVI_SYS_IonFlushCache(phys,base(),LK_TPU_BYTES))throw std::runtime_error("Initial shared cache flush failed");
  return {views[0],views[1]};
 }
 void invalidate(){if(CVI_SYS_IonInvalidateCache(phys+LK_TPU_PLANES_OFF,base()+LK_TPU_PLANES_OFF,bytes))throw std::runtime_error("Image invalidate failed");}
 void remote(const std::vector<cv::Point2f>& points,std::vector<cv::Point2f>& out,std::vector<unsigned char>& valid){
  auto& m=meta();m.count=points.size();for(unsigned i=0;i<m.count;++i)m.input[i]=m.initial[i]={points[i].x,points[i].y};
  auto& c=*reinterpret_cast<tpu_probe_control*>(base());c.magic=TPU_PROBE_MAGIC;c.version=TPU_PROBE_VERSION;c.operation=TPU_PROBE_LK;c.timeout_us=10000;c.completed=0;c.status=-999;
  if(CVI_SYS_IonFlushCache(phys,base(),TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("Control flush failed");
  uint32_t address=phys;
  if(ioctl(fd,_IOW('T',2,uint32_t),&address) || CVI_SYS_IonInvalidateCache(phys,base(),TPU_PROBE_CONTROL_BYTES) || c.status || c.completed!=1)throw std::runtime_error("TPU LK failed");
  out.clear();valid.clear();for(unsigned i=0;i<m.count;++i){out.emplace_back(m.output[i].x,m.output[i].y);valid.push_back(m.valid[i]);}
 }
};
#endif
int main(int argc,char **argv){
 try{
  cv::setNumThreads(1);
  if(argc==2 && std::string(argv[1])=="--selftest"){selftest();return 0;}
  if(argc<5 || argc>6){std::cerr<<"Usage: window_lk DATASET OUTPUT_DIR FRAMES_CSV CALLS [--tpu]\n";return 2;}
  std::string root=argv[1],output=argv[2];unsigned calls=std::stoul(argv[4]);bool tpu=argc==6 && std::string(argv[5])=="--tpu";
  if(!calls || calls>100)throw std::runtime_error("Invalid call count");
#ifndef CPU_WINDOW_LK_DEVICE
  if(tpu)throw std::runtime_error("Host build has no TPU");
#endif
  std::ifstream input(root+"/stereo.csv");std::string line;std::vector<std::vector<std::string>> rows;
  while(std::getline(input,line))if(!line.empty()&&line[0]!='#')rows.push_back(split(line));
  std::ofstream timings(output+"/timings.csv"),details(output+"/points.csv");
  if(!timings || !details)throw std::runtime_error("Create output directory first");
  timings<<"frame,camera,gap,group,count,trial,mode,wall_ms,cpu_ms,gradient_ms,lk_ms,sync_ms,device_ms,valid,gradient_pixels\n"<<std::setprecision(12);
  details<<"frame,camera,gap,group,count,point,x,y,cpu_valid,tpu_valid,cpu_x,cpu_y,tpu_x,tpu_y,tpu_iterations\n"<<std::setprecision(12);
#ifdef CPU_WINDOW_LK_DEVICE
  Shared shared(tpu);
#endif
  unsigned cases=0,points_checked=0;
  for(const auto& value:split(argv[3])){
   unsigned frame=std::stoul(value);
   for(unsigned camera=0;camera<2;++camera)for(unsigned gap:{1u,2u}){
    if(frame+gap>=rows.size())throw std::runtime_error("Frame outside dataset");
    cv::Mat a=cv::imread(root+"/"+rows[frame][camera+1],0),b=cv::imread(root+"/"+rows[frame+gap][camera+1],0);
    if(a.empty() || b.empty())throw std::runtime_error("Image read failed");
    cv::equalizeHist(a,a);cv::equalizeHist(b,b);
    std::vector<cv::Mat> gray,next,full;cv::buildOpticalFlowPyramid(a,gray,cv::Size(16,15),4,false);cv::buildOpticalFlowPyramid(b,next,cv::Size(16,15),4,false);cv::buildOpticalFlowPyramid(a,full,cv::Size(16,15),4,true);
    std::vector<cv::KeyPoint> keys;cv::FAST(a,keys,50,true);size_t selected=std::min<size_t>(75,keys.size());if(selected<32)throw std::runtime_error("Fewer than 32 FAST points");
    std::vector<cv::Point2f> seeds;for(size_t k=0;k<selected;++k)seeds.push_back(keys[k*keys.size()/selected].pt);
    cv::cornerSubPix(a,seeds,cv::Size(5,5),cv::Size(-1,-1),cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,20,.03));
    window_lk::Workspace heap(gray);
#ifdef CPU_WINDOW_LK_DEVICE
    auto ion=shared.pack(gray,next);window_lk::Workspace mapped(ion.first);
#endif
    for(unsigned count:{16u,32u})for(unsigned group=0;group<2;++group){
     size_t first=group?seeds.size()-count:0;std::vector<cv::Point2f> pts(seeds.begin()+first,seeds.begin()+first+count),out,ref,remote;
     std::vector<unsigned char> valid,status,rv;full_track(full,next,pts,ref,status);
     heap.prepare(gray,pts);heap.track(next,pts,out,valid);verify(heap,full,pts,out,valid,ref,status);
#ifdef CPU_WINDOW_LK_DEVICE
     mapped.prepare(ion.first,pts);mapped.track(ion.second,pts,out,valid);verify(mapped,full,pts,out,valid,ref,status);
     if(tpu){shared.remote(pts,remote,rv);for(unsigned i=0;i<count;++i)details<<frame<<','<<camera<<','<<gap<<','<<(group?"tail":"head")<<','<<count<<','<<i<<','<<pts[i].x<<','<<pts[i].y<<','<<int(status[i])<<','<<int(rv[i])<<','<<ref[i].x<<','<<ref[i].y<<','<<remote[i].x<<','<<remote[i].y<<','<<shared.meta().iterations[i]<<'\n';}
#endif
     for(unsigned trial=0;trial<=calls;++trial){
      unsigned modes=2;
#ifdef CPU_WINDOW_LK_DEVICE
      modes=tpu?4:3;
#endif
      for(unsigned order=0;order<modes;++order){unsigned mode=(order+trial+cases)%modes;double start=wall(),c0=cpu(),gradient=0,lk=0,sync=0,device=0;size_t pixels=0;const char *name="";
       if(mode==0){name="full_precomputed";full_track(full,next,pts,out,valid);lk=wall()-start;}
       if(mode==1){name="window_heap";pixels=heap.prepare(gray,pts);double t=wall();gradient=t-start;heap.track(next,pts,out,valid);lk=wall()-t;}
#ifdef CPU_WINDOW_LK_DEVICE
       if(mode==2){name="window_ion_sync";shared.invalidate();double t=wall();sync=t-start;pixels=mapped.prepare(ion.first,pts);double t2=wall();gradient=t2-t;mapped.track(ion.second,pts,out,valid);lk=wall()-t2;}
       if(mode==3){name="tpu";shared.remote(pts,out,valid);device=shared.meta().total_ticks/25000.;}
#endif
       double elapsed=wall()-start,used=cpu()-c0;
       if(mode!=3 && (valid!=status || out!=ref))throw std::runtime_error("Repeated CPU result mismatch");
       if(trial)timings<<frame<<','<<camera<<','<<gap<<','<<(group?"tail":"head")<<','<<count<<','<<trial<<','<<name<<','<<elapsed<<','<<used<<','<<gradient<<','<<lk<<','<<sync<<','<<device<<','<<std::count(valid.begin(),valid.end(),1)<<','<<pixels<<'\n';
      }
     }
     ++cases;points_checked+=count;
    }
    std::cout<<"PAIR frame="<<frame<<" camera="<<camera<<" gap="<<gap<<" cases="<<cases<<std::endl;
   }
  }
  std::cout<<"DONE cases="<<cases<<" cpu_reference_points="<<points_checked<<" CPU_EXACT=1\n";return 0;
 }catch(const std::exception& e){std::cerr<<"CPU window LK: "<<e.what()<<'\n';return 1;}
}

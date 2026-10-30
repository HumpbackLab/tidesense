// Host correctness test for the migrated pixel-only frontend against original TrackKLT.
#include "front_shared.h"
#include "front_detail.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include "track/TrackKLT.h"
#include "cam/CamRadtan.h"
#ifdef FRONT_DEVICE_TEST
#include "tpu_lk.hpp"
#include "cvi_sys.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
extern "C" int lk_tpu_make_jobs(unsigned char*,uint64_t);
#ifdef FRONT_TEST_IVE
#include "../c906-ive/shared.h"
static unsigned verify_ive(unsigned char *base,CVI_U64 physical,int fd,const lk_tpu_level &level,const cv::Mat &expected){
  CVI_U64 arena_phy=0;void *mapping=nullptr;
  if(CVI_SYS_IonAlloc_Cached(&arena_phy,&mapping,"front_ive_test",IVE_ARENA_BYTES) || !mapping || arena_phy>0x8ff40000u-IVE_ARENA_BYTES)throw std::runtime_error("IVE arena allocation");
  auto *arena=(unsigned char*)mapping;std::memset(arena,0xa5,IVE_ARENA_BYTES);
  unsigned image_offset=level.planes[0]+LK_TPU_BORDER*level.row_bytes+LK_TPU_BORDER;
  unsigned errors=0;
  if(((physical+image_offset)|level.row_bytes)&15u)throw std::runtime_error("IVE input is not aligned");
  // Only the reference receives a host copy. The direct job reads the exact
  // gray buffer written by TPU LUT, with its original halo and row pitch.
  std::memcpy(arena+IVE_PACK_OFF,expected.data,expected.total());
  for(unsigned index:{0u,2u,3u}){
    auto *program=(ive_program*)(arena+IVE_PROGRAM_OFF+index*IVE_PROGRAM_BYTES);
    if(ive_make_program(program,index!=0,level.width,level.height,index==3?level.width:level.row_bytes,
        index==3?arena_phy+IVE_PACK_OFF:physical+image_offset,
        arena_phy+(index?IVE_ST_OUT_OFF:IVE_HIST_OFF),arena_phy+IVE_SCRATCH_OFF,25))throw std::runtime_error("IVE program generation");
  }
  if(CVI_SYS_IonFlushCache(arena_phy,arena,IVE_ARENA_BYTES))throw std::runtime_error("IVE arena clean");
  auto *control=(ive_control*)(base+IVE_META_OFF);auto *ctl=(tpu_probe_control*)base;
  std::vector<unsigned char> direct(expected.total());
  for(unsigned index:{0u,2u,3u}){
    std::memset(control,0,sizeof(*control));control->magic=IVE_MAGIC;control->version=IVE_VERSION;
    control->arena=arena_phy;control->arena_bytes=IVE_ARENA_BYTES;control->flags=IVE_FLAG_HIST|IVE_FLAG_ST;
    control->width=level.width;control->height=level.height;control->stride=level.row_bytes;control->test_program=index;control->repeats=20;
    ctl->operation=IVE_PROBE_OPERATION;ctl->status=-99;ctl->completed=0;
    if(CVI_SYS_IonFlushCache(physical+IVE_META_OFF,control,sizeof(*control)) || CVI_SYS_IonFlushCache(physical,base,TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("IVE control clean");
    uint32_t address=physical;
    if(ioctl(fd,_IOW('T',5,uint32_t),&address) || CVI_SYS_IonInvalidateCache(physical,base,TPU_PROBE_CONTROL_BYTES) || CVI_SYS_IonInvalidateCache(physical+IVE_META_OFF,control,sizeof(*control)) || ctl->status || ctl->completed!=20)throw std::runtime_error("IVE execution");
    if(CVI_SYS_IonInvalidateCache(arena_phy,arena,IVE_ARENA_BYTES))throw std::runtime_error("IVE output invalidate");
    uint64_t total=0;for(unsigned i=0;i<20;++i)total+=control->run_ticks[i];
    if(index==0){unsigned histogram[256]={0};for(size_t i=0;i<expected.total();++i)++histogram[expected.data[i]];
      auto *bins=(unsigned*)(arena+IVE_HIST_OFF);for(unsigned i=0;i<256;++i)errors+=bins[i]!=histogram[i];
    }else if(index==2)std::memcpy(direct.data(),arena+IVE_ST_OUT_OFF,direct.size());
    else for(size_t i=0;i<direct.size();++i)errors+=direct[i]!=arena[IVE_ST_OUT_OFF+i];
    for(unsigned i=IVE_SCRATCH_OFF+IVE_SCRATCH_BYTES;i<IVE_ST_OUT_OFF;++i)errors+=arena[i]!=0xa5;
    for(unsigned i=IVE_ST_OUT_OFF+expected.total();i<IVE_PACK_OFF;++i)errors+=arena[i]!=0xa5;
    std::cout<<"IVE_DIRECT height="<<level.height<<" stride="<<level.row_bytes<<" image_offset="<<image_offset<<" kind="<<(index==0?"hist":index==2?"st-direct":"st-contiguous-reference")<<" mean_ms="<<total/25000.0/20<<" pack_ticks="<<control->pack_ticks<<" errors="<<errors<<'\n';
  }
  std::memset(control,0,sizeof(*control));
  if(CVI_SYS_IonFlushCache(physical+IVE_META_OFF,control,sizeof(*control)))throw std::runtime_error("IVE control clear");
  CVI_SYS_IonFree(arena_phy,mapping);return errors;
}
#endif
#endif
struct Reference:ov_core::TrackKLT {
  Reference(int height,int threshold):TrackKLT({{0,std::make_shared<ov_core::CamRadtan>(640,height)},
      {1,std::make_shared<ov_core::CamRadtan>(640,height)}},75,0,true,HistogramMethod::HISTOGRAM,threshold,5,5,15){}
  using TrackKLT::perform_detection_stereo;
  void seed(size_t id){currid=id;}
};
static probe_u64 ticks(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()/40;}
static std::vector<cv::Mat> pyr(void *memory,const lk_tpu_level *l,unsigned n){
  std::vector<cv::Mat> out;for(unsigned k=0;k<n;++k){
    cv::Mat all(l[k].height+2*LK_TPU_BORDER,l[k].row_bytes,CV_8UC1,(unsigned char*)memory+l[k].planes[0]);
    out.push_back(all(cv::Rect(LK_TPU_BORDER,LK_TPU_BORDER,l[k].width,l[k].height)));}return out;
}
#ifndef FRONT_DEVICE_TEST
static int flow(void *memory,const lk_tpu_level *a,const lk_tpu_level *b,unsigned levels,
    const front_point *points,unsigned n,lk_tpu_point *out,unsigned char *valid){
  std::vector<cv::Point2f> in,dst;for(unsigned i=0;i<n;++i)in.emplace_back(points[i].x,points[i].y);dst=in;
  std::vector<unsigned char> status;std::vector<float> error;
  cv::calcOpticalFlowPyrLK(pyr(memory,a,levels),pyr(memory,b,levels),in,dst,status,error,cv::Size(16,15),5,
      cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
  for(unsigned i=0;i<n;++i){out[i]={dst[i].x,dst[i].y};valid[i]=status[i];}return 0;
}
#endif
int main(int argc,char **argv){
  if(argc<2 || argc>4)return 2;
  const int height=argc>2?std::stoi(argv[2]):480,threshold=argc>3?std::stoi(argv[3]):50;
  if((height!=400 && height!=480) || threshold<0 || threshold>255)return 2;
#ifdef FRONT_DEVICE_TEST
  setenv("OPENVINS_TPU_LK","1",1);
#else
  setenv("OPENVINS_TPU_LK","0",1);
#endif
  cv::setNumThreads(1);
  std::ifstream file(std::string(argv[1])+"/stereo.csv");std::string line;std::vector<std::vector<std::string>> rows;
  while(std::getline(file,line))if(!line.empty()&&line[0]!='#'){std::istringstream s(line);std::vector<std::string> r;std::string f;while(std::getline(s,f,','))r.push_back(f);rows.push_back(r);}
  std::vector<unsigned char> memory(FRONT_BYTES);auto *base=memory.data();
#ifdef FRONT_DEVICE_TEST
  CVI_U64 physical=0;void *allocation=nullptr;
  if(CVI_SYS_IonAlloc_Cached(&physical,&allocation,"front_test",FRONT_BYTES) || physical>0x8ff40000u-FRONT_BYTES)return 2;
  base=(unsigned char*)allocation;std::memset(base,0,FRONT_BYTES);
#endif
  auto &f=*(front_control*)(base+FRONT_META_OFF);
#ifndef FRONT_DEVICE_TEST
  front_ops ops={ticks,front_cpu_pyramid,flow};
#endif
  double maximum=0;unsigned failures=0;
#ifdef FRONT_DEVICE_TEST
  if(lk_tpu_make_jobs(base,physical) || front_make_jobs(base,physical,640,height))return 2;
  front_control job_template=f;
  if(CVI_SYS_IonFlushCache(physical,base,FRONT_BYTES))return 2;
  int fd=open("/dev/c906-tpu-probe",O_RDWR);if(fd<0)return 2;
#endif
  for(unsigned frame:{0u,200u,1000u,1200u,1500u,1900u}){
    std::vector<cv::Mat> images,pyramids[2];
    for(unsigned cam=0;cam<2;++cam){auto im=cv::imread(std::string(argv[1])+"/"+rows.at(frame).at(cam+1),0);if(im.empty() || im.cols!=640 || im.rows<height)return 2;
      im=im(cv::Rect(0,0,640,height)).clone();
      std::memcpy(base+FRONT_RAW_OFF+cam*FRONT_PIXELS,im.data,im.total());cv::Mat eq;cv::equalizeHist(im,eq);images.push_back(eq);cv::buildOpticalFlowPyramid(eq,pyramids[cam],cv::Size(16,15),5,false);}
    std::memset(&f,0,sizeof(f));f.magic=FRONT_MAGIC;f.version=FRONT_VERSION;f.width=640;f.height=height;f.histogram=1;f.num_features=75;f.grid_x=f.grid_y=5;f.min_px_dist=15;f.threshold=threshold;f.currid=1;f.timestamp=frame+1;
#ifdef FRONT_DEVICE_TEST
    f.equalize_dma_bytes=job_template.equalize_dma_bytes;f.equalize_patch_count=job_template.equalize_patch_count;
    std::memcpy(f.equalize_patch,job_template.equalize_patch,sizeof(f.equalize_patch));
    std::memcpy(f.pyramid_dma_bytes,job_template.pyramid_dma_bytes,sizeof(f.pyramid_dma_bytes));
    std::memcpy(f.pyramid_patch_count,job_template.pyramid_patch_count,sizeof(f.pyramid_patch_count));
    std::memcpy(f.pyramid_patch,job_template.pyramid_patch,sizeof(f.pyramid_patch));
    const bool abandon=std::getenv("FRONT_TEST_ABANDON")!=nullptr;
    if(abandon){f.deferred_snapshot=1;
      auto *snapshot=(front_snapshot*)(base+FRONT_SNAPSHOT_OFF);snapshot->ready=0;
      CVI_SYS_IonFlushCache(physical+FRONT_SNAPSHOT_OFF,snapshot,sizeof(*snapshot));}
    auto &ctl=*(tpu_probe_control*)base;ctl.magic=TPU_PROBE_MAGIC;ctl.version=TPU_PROBE_VERSION;ctl.operation=TPU_PROBE_FRONT;ctl.timeout_us=10000;ctl.completed=0;ctl.status=-99;
    CVI_SYS_IonFlushCache(physical,base,TPU_PROBE_CONTROL_BYTES);
    CVI_SYS_IonFlushCache(physical+FRONT_META_OFF,&f,sizeof(f));
    CVI_SYS_IonFlushCache(physical+FRONT_RAW_OFF,base+FRONT_RAW_OFF,2*FRONT_PIXELS);
    CVI_SYS_IonFlushCache(physical+FRONT_INPUT_MASK_OFF,base+FRONT_INPUT_MASK_OFF,2*FRONT_PIXELS);
    uint32_t address=physical;auto before=ticks();int ret=ioctl(fd,_IOW('T',4,uint32_t),&address);double elapsed=(ticks()-before)/25000.0;
    CVI_SYS_IonInvalidateCache(physical,base,TPU_PROBE_CONTROL_BYTES);
    CVI_SYS_IonInvalidateCache(physical+FRONT_META_OFF,&f,sizeof(f));
    CVI_SYS_IonInvalidateCache(physical+FRONT_STATE_OFF,base+FRONT_STATE_OFF,sizeof(front_state));
    CVI_SYS_IonInvalidateCache(physical+FRONT_PYR_OFF,base+FRONT_PYR_OFF,4*FRONT_CAM_BYTES);
    std::cout<<"DEVICE_FRONT frame="<<frame<<" ret="<<ret<<" status="<<ctl.status<<" wall_ms="<<elapsed<<" equalize_ms="<<f.equalize_ticks/25000.0<<" pyr_ms="<<f.pyramid_ticks/25000.0<<" detect_ms="<<f.detect_ticks/25000.0<<" lk_ms="<<f.lk_ticks/25000.0<<std::endl;
    if(!ret)ret=ctl.status;
    auto *detail=front_detail_at(base);
    if(detail->magic==FRONT_DETAIL_MAGIC){
      std::cout<<"DEVICE_DETAIL flags="<<detail->flags;
#define PRINT(n) std::cout<<" " #n "_ms="<<detail->n##_ticks/25000.0;
      FRONT_DETAIL_TIMES(PRINT)
#undef PRINT
      std::cout<<'\n';
    }
    if(abandon){
      std::cout<<"SNAPSHOT_TIMEOUT_TEST status="<<ret<<" wall_ms="<<elapsed<<std::endl;
      close(fd);CVI_SYS_IonFree(physical,base);
      return ret==-56 && elapsed>=500 && elapsed<1500 ? 0 : 1;
    }
#else
    int ret=front_run(base,&ops);
#endif
    if(ret){std::cerr<<"front_run "<<ret<<'\n';return 1;}
    auto &state=*(front_state*)(base+FRONT_STATE_OFF);unsigned slot=state.timestamp[0]==f.timestamp?0:1;
    for(unsigned cam=0;cam<2;++cam){auto actual=pyr(base,state.pyramid[slot][cam],state.levels);
      for(unsigned l=0;l<state.levels;++l){
        const auto &level=state.pyramid[slot][cam][l];
        if(FRONT_ALIGNED_IMAGES && (((level.planes[0]+LK_TPU_BORDER*level.row_bytes+LK_TPU_BORDER)|level.row_bytes)&15u))throw std::runtime_error("Pyramid layout alignment");
        if(cv::norm(actual[l],pyramids[cam][l],cv::NORM_INF)!=0){std::cerr<<"Pyramid differs "<<frame<<' '<<cam<<' '<<l<<'\n';++failures;}
        cv::Mat border;cv::copyMakeBorder(pyramids[cam][l],border,LK_TPU_BORDER,LK_TPU_BORDER,LK_TPU_BORDER,LK_TPU_BORDER,cv::BORDER_REFLECT_101|cv::BORDER_ISOLATED);
        cv::Mat padded(level.height+2*LK_TPU_BORDER,level.width+2*LK_TPU_BORDER,CV_8UC1,base+level.planes[0],level.row_bytes);
        if(cv::norm(padded,border,cv::NORM_INF)!=0){std::cerr<<"Halo differs "<<frame<<' '<<cam<<' '<<l<<'\n';++failures;}
      }
#ifdef FRONT_TEST_IVE
      failures+=verify_ive(base,physical,fd,state.pyramid[slot][cam][0],images[cam]);
#endif
    }
    Reference reference(height,threshold);std::vector<cv::KeyPoint> points[2];std::vector<size_t> ids[2];auto mask=cv::Mat::zeros(height,640,CV_8UC1);
    reference.perform_detection_stereo(pyramids[0],pyramids[1],mask,mask,0,1,points[0],points[1],ids[0],ids[1]);
    double max_cam[2]={0,0};
    for(unsigned cam=0;cam<2;++cam){if(points[cam].size()!=f.count[cam]){std::cerr<<"Count differs frame="<<frame<<" cam="<<cam<<" ref="<<points[cam].size()<<" got="<<f.count[cam]<<'\n';++failures;}
      for(unsigned i=0;i<std::min<size_t>(points[cam].size(),f.count[cam]);++i){
        double d=cv::norm(points[cam][i].pt-cv::Point2f(f.old[cam][i].x,f.old[cam][i].y));maximum=std::max(maximum,d);max_cam[cam]=std::max(max_cam[cam],d);
        bool equivalent=d<=.005;
#ifdef FRONT_DEVICE_TEST
        // A tiny subpixel rounding change can cross a BF16 LK sampling boundary.
        // Verify the result again with identical input, rather than relaxing LK tolerance.
        if(!equivalent && cam==1)for(unsigned j=0;j<f.count[0];++j)if(f.old[0][j].id==f.old[cam][i].id){
          auto source=std::find(ids[0].begin(),ids[0].end(),f.old[0][j].id);
          if(source==ids[0].end())break;
          cv::Point2f input(f.old[0][j].x,f.old[0][j].y);
          double source_delta=cv::norm(input-points[0][source-ids[0].begin()].pt);
          if(source_delta==0 || source_delta>.005)break;
          auto same=ov_core::TpuLk::prepare(pyramids[0],pyramids[1],{input},{input});
          ov_core::TpuLk::execute(same);
          double same_delta=cv::norm(same->output[0]-cv::Point2f(f.old[1][i].x,f.old[1][i].y));
          equivalent=same->status[0] && same_delta<=.005;
          std::cout<<"ROUNDING_CHECK source_delta="<<source_delta<<" propagated_delta="<<d<<" matched_input_delta="<<same_delta<<'\n';
          break;
        }
#endif
        if(!equivalent || ids[cam][i]!=f.old[cam][i].id){if(failures<8)std::cerr<<"Point differs frame="<<frame<<" cam="<<cam<<" i="<<i<<" delta="<<d<<" IDs="<<ids[cam][i]<<'/'<<f.old[cam][i].id<<'\n';++failures;}}
    }
    std::cout<<"FRONT_TEST frame="<<frame<<" counts="<<f.count[0]<<'/'<<f.count[1]<<" cam_delta="<<max_cam[0]<<'/'<<max_cam[1]<<" max_delta="<<maximum<<" failures="<<failures<<'\n';
  }
#ifdef FRONT_DEVICE_TEST
  close(fd);CVI_SYS_IonFree(physical,base);
#endif
  return failures?1:0;
}

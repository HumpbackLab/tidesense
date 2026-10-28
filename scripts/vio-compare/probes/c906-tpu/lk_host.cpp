// Standalone correctness/speed experiment; does not change OpenVINS.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include "cvi_sys.h"
#include "lk_shared.h"
extern "C" int lk_tpu_make_jobs(uint8_t *, uint64_t);
int lk_tpu_overlap(int, uint32_t, void *);
#define LK_IOCTL _IOW('T', 2, uint32_t)
static_assert(sizeof(tpu_probe_control)<=LK_TPU_META_OFF,"probe control overlaps LK metadata");
static_assert(LK_TPU_META_OFF+sizeof(lk_tpu_control)<=TPU_PROBE_CONTROL_BYTES,"LK metadata exceeds control region");
static double now_ms() {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static float sample(const std::vector<cv::Mat> &pa,const std::vector<cv::Mat> &pb,
                    unsigned level,unsigned plane,float x,float y) {
    int ix=std::floor(x),iy=std::floor(y);float fx=x-ix,fy=y-iy;
    const cv::Mat &im=plane<2?(plane?pb[level*2]:pa[level*2]):pa[level*2+1];
    auto pixel=[&](int xx,int yy) {
        if(plane<2) {
            xx=cv::borderInterpolate(xx,im.cols,cv::BORDER_REFLECT_101);
            yy=cv::borderInterpolate(yy,im.rows,cv::BORDER_REFLECT_101);
            return float(im.at<uint8_t>(yy,xx))-128;
        }
        if(xx<0 || yy<0 || xx>=im.cols || yy>=im.rows)return 0.0f;
        return float(im.at<cv::Vec2s>(yy,xx)[plane-2])/32;
    };
    return (1-fy)*((1-fx)*pixel(ix,iy)+fx*pixel(ix+1,iy))+
        fy*((1-fx)*pixel(ix,iy+1)+fx*pixel(ix+1,iy+1));
}
static bool inside(const lk_tpu_level &l,float x,float y) {
    int ix=std::floor(x-7.5f),iy=std::floor(y-7.0f);
    return ix>=-16 && iy>=-15 && ix<int(l.width) && iy<int(l.height);
}
static void reference(const std::vector<cv::Mat> &pa,const std::vector<cv::Mat> &pb,lk_tpu_control &m) {
    for(unsigned p=0;p<m.count;++p) {
        float nx=m.initial[p].x/(1u<<(m.levels-1)),ny=m.initial[p].y/(1u<<(m.levels-1));
        m.valid[p]=1;m.iterations[p]=0;
        for(int level=m.levels-1;level>=0;--level) {
            if(level!=int(m.levels)-1){nx*=2;ny*=2;}
            auto &l=m.level[level];float x=m.input[p].x/(1u<<level),y=m.input[p].y/(1u<<level);
            if(!inside(l,x,y)){if(!level)m.valid[p]=0;continue;}
            float tmpl[240],gx[240],gy[240],a=0,b=0,c=0;
            for(int yy=0,k=0;yy<15;++yy)for(int xx=0;xx<16;++xx,++k) {
                tmpl[k]=sample(pa,pb,level,0,x-7.5f+xx,y-7+yy);
                gx[k]=sample(pa,pb,level,2,x-7.5f+xx,y-7+yy);
                gy[k]=sample(pa,pb,level,3,x-7.5f+xx,y-7+yy);
                a+=gx[k]*gx[k];b+=gx[k]*gy[k];c+=gy[k]*gy[k];
            }
            float det=a*c-b*b,eig=(a+c-std::sqrt((a-c)*(a-c)+4*b*b))/(2*240);
            if(det<1e-7f || eig<0.1024f){if(!level)m.valid[p]=0;continue;}
            float pdx=0,pdy=0;
            for(unsigned it=0;it<m.max_iterations;++it) {
                if(!inside(l,nx,ny)){if(!level)m.valid[p]=0;break;}
                float bx=0,by=0;
                for(int yy=0,k=0;yy<15;++yy)for(int xx=0;xx<16;++xx,++k) {
                    float r=tmpl[k]-sample(pa,pb,level,1,nx-7.5f+xx,ny-7+yy);
                    bx+=gx[k]*r;by+=gy[k]*r;
                }
                float dx=(c*bx-b*by)/det,dy=(a*by-b*bx)/det;
                nx+=dx;ny+=dy;++m.iterations[p];
                if(dx*dx+dy*dy<=0.0001f)break;
                if(it && std::abs(dx+pdx)<0.01f && std::abs(dy+pdy)<0.01f){nx-=dx*.5f;ny-=dy*.5f;break;}
                pdx=dx;pdy=dy;
            }
        }
        m.output[p]={nx,ny};
    }
}
static void pack(uint8_t *base,lk_tpu_control &m,const std::vector<cv::Mat> &a,
                 const std::vector<cv::Mat> &b) {
    unsigned off=LK_TPU_PLANES_OFF;
    for(unsigned l=0;l<m.levels;++l) {
        auto &level=m.level[l];const cv::Mat &im=a[l];
        level.width=im.cols;level.height=im.rows;level.row_bytes=im.cols+2*LK_TPU_BORDER;
        for(unsigned p=0;p<2;++p) {
            const cv::Mat &src=p?b[l]:im;
            cv::Mat border;cv::copyMakeBorder(src,border,LK_TPU_BORDER,LK_TPU_BORDER,
                LK_TPU_BORDER,LK_TPU_BORDER,cv::BORDER_REFLECT_101|cv::BORDER_ISOLATED);
            off=(off+63)&~63u;unsigned bytes=border.rows*level.row_bytes;
            if(off+bytes>LK_TPU_BYTES)throw std::runtime_error("pyramids exceed shared memory");
            level.planes[p]=off;
            for(int y=0;y<border.rows;++y)std::memcpy(base+off+y*level.row_bytes,border.ptr(y),level.row_bytes);
            off+=bytes;
        }
    }
}
static double quantile(std::vector<double> v,double q) {
    if(v.empty())return NAN;
    std::sort(v.begin(),v.end());return v[size_t((v.size()-1)*q)];
}
int main(int argc,char **argv) {
    if(argc<2 || argc>6) {
        std::fprintf(stderr,"usage: %s synthetic|prev.png [next.png] [points=150] [calls=3] [output.csv]\n",argv[0]);return 2;
    }
    CVI_U64 phys=0;void *allocation=nullptr;int fd=-1,ret=1;
    try {
        cv::setNumThreads(1);cv::Mat a,b;bool synthetic=std::string(argv[1])=="synthetic";
        int nextarg=synthetic?2:3;
        if(synthetic) {
            a=cv::Mat(400,640,CV_8U);cv::RNG rng(100);rng.fill(a,cv::RNG::UNIFORM,0,256);
            cv::GaussianBlur(a,a,cv::Size(5,5),1.0);
            cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,2.35,0,1,-1.7);
            cv::warpAffine(a,b,transform,a.size(),cv::INTER_LINEAR,cv::BORDER_REFLECT_101);
        } else {
            if(argc<3)throw std::runtime_error("missing next image");
            a=cv::imread(argv[1],cv::IMREAD_GRAYSCALE);b=cv::imread(argv[2],cv::IMREAD_GRAYSCALE);
        }
        if(a.empty() || b.empty() || a.size()!=b.size() || a.cols>640 || a.rows>480)
            throw std::runtime_error("expected matching grayscale images <=640x480");
        unsigned count=argc>nextarg?std::stoul(argv[nextarg]):150;
        unsigned calls=argc>nextarg+1?std::stoul(argv[nextarg+1]):3;
        if(!count || count>160 || !calls || calls>20)throw std::runtime_error("invalid count/calls");
        if(CVI_SYS_IonAlloc_Cached(&phys,&allocation,"c906_tpu_lk",LK_TPU_BYTES) || !allocation ||
           phys>0x8ff40000u-LK_TPU_BYTES)throw std::runtime_error("ION allocation failed");
        auto *base=static_cast<uint8_t *>(allocation);std::memset(base,0,LK_TPU_BYTES);
        auto &ctl=*reinterpret_cast<tpu_probe_control *>(base);
        auto &m=*reinterpret_cast<lk_tpu_control *>(base+LK_TPU_META_OFF);
        ctl.magic=TPU_PROBE_MAGIC;ctl.version=TPU_PROBE_VERSION;ctl.operation=TPU_PROBE_LK;ctl.timeout_us=10000;
        m.version=LK_TPU_VERSION;m.max_iterations=30;m.batch=LK_TPU_BATCH;
        double t=now_ms();std::vector<cv::Mat> pa,pb;
        std::vector<cv::Mat> pna,pnb;
        double pyramid_ms=0,offload_pyramid_ms=0;
        auto make_pyramids=[&](bool derivatives) {
            t=now_ms();
            int la=cv::buildOpticalFlowPyramid(a,derivatives?pa:pna,cv::Size(16,15),5,derivatives);
            int lb=cv::buildOpticalFlowPyramid(b,derivatives?pb:pnb,cv::Size(16,15),5,derivatives);
            m.levels=std::min(la,lb)+1;return now_ms()-t;
        };
        make_pyramids(true);make_pyramids(false);
        for(unsigned call=0;call<calls;++call) {
            if(call%2){offload_pyramid_ms+=make_pyramids(false);pyramid_ms+=make_pyramids(true);}
            else {pyramid_ms+=make_pyramids(true);offload_pyramid_ms+=make_pyramids(false);}
        }
        pyramid_ms/=calls;offload_pyramid_ms/=calls;
        if(m.levels>LK_TPU_LEVELS)throw std::runtime_error("unexpected pyramid count");
        std::vector<cv::KeyPoint> keys;cv::FAST(a,keys,20,true);
        std::vector<cv::Point2f> points;
        for(unsigned k=0;k<std::min(count,unsigned(keys.size()));++k) {
            auto p=keys[size_t(k)*keys.size()/std::min(count,unsigned(keys.size()))].pt;points.push_back(p);
            m.input[k]={p.x,p.y};m.initial[k]=m.input[k];
        }
        m.count=points.size();if(!m.count)throw std::runtime_error("no features");
        pack(base,m,pna,pnb);double pack_ms=0;
        for(unsigned call=0;call<calls;++call){t=now_ms();pack(base,m,pna,pnb);pack_ms+=now_ms()-t;}
        pack_ms/=calls;
        std::printf("LK input points=%u levels=%u batch=%u\n",m.count,m.levels,m.batch);
        t=now_ms();if(lk_tpu_make_jobs(base,phys))throw std::runtime_error("TPU command generation failed");
        double compile_ms=now_ms()-t;
        lk_tpu_control scalar=m;
        t=now_ms();reference(pa,pb,scalar);double scalar_ms=now_ms()-t;
        std::vector<cv::Point2f> next;std::vector<uint8_t> status;std::vector<float> errors;
        double cv_ms=0;
        for(unsigned call=0;call<calls+1;++call) {
            next=points;t=now_ms();cv::calcOpticalFlowPyrLK(pa,pb,points,next,status,errors,cv::Size(16,15),5,
                cv::TermCriteria(cv::TermCriteria::COUNT|cv::TermCriteria::EPS,30,.01),cv::OPTFLOW_USE_INITIAL_FLOW);
            if(call)cv_ms+=now_ms()-t;
        }
        cv_ms/=calls;
        fd=open("/dev/c906-tpu-probe",O_RDWR);if(fd<0)throw std::runtime_error("open bridge failed");
        double wall_sum=0,flush_sum=0;
        for(unsigned call=0;call<calls;++call) {
            ctl.status=-999;ctl.completed=0;t=now_ms();
            if(CVI_SYS_IonFlushCache(phys,base,LK_TPU_BYTES))throw std::runtime_error("flush failed");
            flush_sum+=now_ms()-t;t=now_ms();uint32_t addr=phys;
            if(ioctl(fd,LK_IOCTL,&addr))throw std::runtime_error("mailbox failed");
            if(CVI_SYS_IonInvalidateCache(phys,base,TPU_PROBE_CONTROL_BYTES))throw std::runtime_error("invalidate failed");
            wall_sum+=now_ms()-t;
            std::printf("LK remote call=%u status=%d completed=%u total_ms=%.3f prep_ms=%.3f tpu_ms=%.3f solve_ms=%.3f submissions=%u valid=%u\n",
                call,ctl.status,ctl.completed,m.total_ticks/25000.0,m.prepare_ticks/25000.0,
                m.execute_ticks/25000.0,m.solve_ticks/25000.0,m.submissions,m.valid_count);
            if(ctl.status || ctl.completed!=1)throw std::runtime_error("C906 LK failed");
        }
        std::vector<double> epe,scalar_epe,truth_epe;unsigned cvvalid=0,shared=0,agree=0;
        std::ofstream csv;
        if(argc>nextarg+2)csv.open(argv[nextarg+2]);
        if(csv)csv<<std::setprecision(9)<<"id,x,y,opencv_valid,scalar_valid,tpu_valid,opencv_x,opencv_y,scalar_x,scalar_y,tpu_x,tpu_y,tpu_iterations\n";
        for(unsigned p=0;p<m.count;++p) {
            cvvalid+=status[p]!=0;agree+=(status[p]!=0)==(m.valid[p]!=0);
            if(status[p] && m.valid[p]) {
                epe.push_back(std::hypot(next[p].x-m.output[p].x,next[p].y-m.output[p].y));shared++;
                if(synthetic)truth_epe.push_back(std::hypot(m.output[p].x-points[p].x-2.35,m.output[p].y-points[p].y+1.7));
            }
            if(status[p] && scalar.valid[p])scalar_epe.push_back(std::hypot(next[p].x-scalar.output[p].x,next[p].y-scalar.output[p].y));
            if(csv)csv<<p<<','<<points[p].x<<','<<points[p].y<<','<<int(status[p])<<','<<int(scalar.valid[p])<<','<<int(m.valid[p])<<','
                <<next[p].x<<','<<next[p].y<<','<<scalar.output[p].x<<','<<scalar.output[p].y<<','<<m.output[p].x<<','<<m.output[p].y<<','<<m.iterations[p]<<'\n';
        }
        std::printf("SUMMARY points=%u cv_valid=%u tpu_valid=%u status_agree=%u shared=%u cv_lk_ms=%.3f scalar_ms=%.3f remote_wall_ms=%.3f flush_ms=%.3f pyramid_ms=%.3f offload_pyramid_ms=%.3f pack_ms=%.3f compile_ms=%.3f epe50=%.4f epe95=%.4f epe_max=%.4f scalar_epe95=%.4f truth_epe95=%.4f\n",
            m.count,cvvalid,m.valid_count,agree,shared,cv_ms,scalar_ms,wall_sum/calls,flush_sum/calls,pyramid_ms,offload_pyramid_ms,pack_ms,compile_ms,
            quantile(epe,.5),quantile(epe,.95),quantile(epe,1),quantile(scalar_epe,.95),quantile(truth_epe,.95));
        const char *overlap=std::getenv("C906_LK_OVERLAP");
        if(overlap && std::strcmp(overlap,"0") && lk_tpu_overlap(fd,phys,base))
            throw std::runtime_error("overlap verification failed");
        ret=0;
    } catch(const std::exception &e) {std::fprintf(stderr,"LK experiment: %s\n",e.what());}
    if(fd>=0)close(fd);
    if(allocation)CVI_SYS_IonFree(phys,allocation);
    return ret;
}

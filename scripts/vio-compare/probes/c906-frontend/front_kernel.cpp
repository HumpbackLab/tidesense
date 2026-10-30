/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pixel-only OpenVINS grid tracker, isolated from calibration and FeatureDatabase.
 * Grid selection follows OpenVINS TrackKLT / Grider_GRID (2018-2023 contributors).
 * FAST score / subpixel refinement follow OpenCV 3.4.20; see LICENSE-OpenCV.txt.
 */
#include "front_shared.h"
#include "front_detail.h"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>

namespace {
struct Scratch {
    unsigned char score[FRONT_PIXELS],updated[FRONT_PIXELS];
    unsigned char close[FRONT_PIXELS/16],grid[FRONT_GRID_MAX*FRONT_GRID_MAX];
    front_point candidates[FRONT_CANDIDATES],extracted[FRONT_POINTS];
    unsigned int histogram[256];unsigned char lut[256];
    float weights[121],patch[169];
    int gaussian[5][FRONT_MAX_WIDTH];
#ifdef FRONT_SPARSE_NMS
    probe_u32 positions[FRONT_PIXELS];
#endif
    front_detail *profile;
    probe_u64 (*clock)();
};
static_assert(sizeof(Scratch)<=FRONT_BYTES-FRONT_SCRATCH_OFF,"Frontend scratch overflows shared allocation");
#ifdef FRONT_PROFILE
#define DETAIL_BEGIN(s) ((s).clock())
#define DETAIL_ADD(s,name,start) ((s).profile->name##_ticks+=(s).clock()-(start))
#else
#define DETAIL_BEGIN(s) probe_u64(0)
#define DETAIL_ADD(s,name,start) ((void)(start))
#endif
static int reflect(int x,int n){while(x<0 || x>=n)x=x<0?-x:2*n-2-x;return x;}
static unsigned char *image(unsigned char *base,const lk_tpu_level &l){return base+l.planes[0]+LK_TPU_BORDER*l.row_bytes+LK_TPU_BORDER;}
static void fill(void *memory,int value,size_t bytes){
#ifdef FRONT_FAST_FILL
    // Aligned scalar 64-bit stores; volatile prevents GCC turning this back
    // into the SDK's byte-at-a-time memset. No unaligned accesses or overrun.
    typedef probe_u64 AliasWord __attribute__((may_alias));
    auto *p=(unsigned char*)memory;unsigned char byte=(unsigned char)value;
    while(bytes && ((uintptr_t)p&7)){*p++=byte;--bytes;}
    auto *words=(volatile AliasWord*)p;probe_u64 word=probe_u64(byte)*0x0101010101010101ull;
    while(bytes>=32){words[0]=word;words[1]=word;words[2]=word;words[3]=word;words+=4;bytes-=32;}
    while(bytes>=8){*words++=word;bytes-=8;}
    p=(unsigned char*)words;while(bytes--)*p++=byte;
#else
    std::memset(memory,value,bytes);
#endif
}
static void lut_from_histogram(const unsigned *bins,unsigned total,unsigned char *lut) {
    unsigned first=0;while(first<256 && !bins[first])++first;
    if(first==256){fill(lut,0,256);return;}
    if(bins[first]==total){fill(lut,first,256);return;}
    float scale=255.f/(total-bins[first]);unsigned sum=0;
    fill(lut,0,first+1);
    for(unsigned i=first+1;i<256;++i){sum+=bins[i];lut[i]=(unsigned char)std::min(255L,lrintf(sum*scale));}
}
static void prepare_lut(const unsigned char *__restrict src,unsigned total,bool histogram,unsigned char *lut) {
    if(!histogram){for(unsigned i=0;i<256;++i)lut[i]=i;return;}
    unsigned bins[4][256];fill(bins,0,sizeof(bins));
    unsigned i=0;for(;i+3<total;i+=4){++bins[0][src[i]];++bins[1][src[i+1]];++bins[2][src[i+2]];++bins[3][src[i+3]];}
    for(;i<total;++i)++bins[0][src[i]];
    for(i=0;i<256;++i)bins[0][i]+=bins[1][i]+bins[2][i]+bins[3][i];
    lut_from_histogram(bins[0],total,lut);
}
static void apply_lut(unsigned char *__restrict dst,const unsigned char *__restrict src,unsigned width,unsigned height,unsigned stride,const unsigned char *lut) {
    for(unsigned y=0;y<height;++y){auto *row=dst+y*stride;const auto *in=src+y*width;unsigned x=0;
        for(;x+3<width;x+=4){row[x]=lut[in[x]];row[x+1]=lut[in[x+1]];row[x+2]=lut[in[x+2]];row[x+3]=lut[in[x+3]];}
        for(;x<width;++x)row[x]=lut[in[x]];
    }
}
static int score(const unsigned char *p,const int *off,int threshold) {
    short d[25];for(int k=0;k<25;++k)d[k]=short(p[0]-p[off[k%16]]);
    int a0=threshold;
    for(int k=0;k<16;k+=2){
        int a=std::min<int>(d[k+1],d[k+2]);a=std::min<int>(a,d[k+3]);if(a<=a0)continue;
        for(int j=4;j<=8;++j)a=std::min<int>(a,d[k+j]);
        a0=std::max(a0,std::min<int>(a,d[k]));a0=std::max(a0,std::min<int>(a,d[k+9]));
    }
    int b0=-a0;
    for(int k=0;k<16;k+=2){
        int b=std::max<int>(d[k+1],d[k+2]);for(int j=3;j<=5;++j)b=std::max<int>(b,d[k+j]);if(b>=b0)continue;
        for(int j=6;j<=8;++j)b=std::max<int>(b,d[k+j]);
        b0=std::min(b0,std::max<int>(b,d[k]));b0=std::min(b0,std::max<int>(b,d[k+9]));
    }
    return -b0-1;
}
static bool nine(unsigned bits){unsigned v=bits|(bits<<16),a=v&(v>>1);a&=a>>2;a&=a>>4;return (a&(v>>8)&65535u)!=0;}
static unsigned fast_roi(const unsigned char *im,unsigned stride,int w,int h,int threshold,Scratch &s) {
    static const int xy[16][2]={{0,3},{1,3},{2,2},{3,1},{3,0},{3,-1},{2,-2},{1,-3},
        {0,-3},{-1,-3},{-2,-2},{-3,-1},{-3,0},{-3,1},{-2,2},{-1,3}};
    int off[16];for(int k=0;k<16;++k)off[k]=xy[k][0]+xy[k][1]*(int)stride;
    unsigned char table[512];for(int k=-255;k<=255;++k)table[k+255]=(k < -threshold)?1:(k>threshold?2:0);
    auto fast_begin=DETAIL_BEGIN(s);
    fill(s.score,0,w*h);
#ifdef FRONT_SPARSE_NMS
    unsigned positions=0;
#endif
    for(int y=3;y<h-3;++y)for(int x=3;x<w-3;++x){
        const auto *p=im+y*stride+x;const auto *tab=table+255-p[0];int d=tab[p[off[0]]]|tab[p[off[8]]];
        if(!d)continue;
        d&=tab[p[off[2]]]|tab[p[off[10]]];d&=tab[p[off[4]]]|tab[p[off[12]]];d&=tab[p[off[6]]]|tab[p[off[14]]];
        if(!d)continue;
        d&=tab[p[off[1]]]|tab[p[off[9]]];d&=tab[p[off[3]]]|tab[p[off[11]]];d&=tab[p[off[5]]]|tab[p[off[13]]];d&=tab[p[off[7]]]|tab[p[off[15]]];
        if(!d)continue;
        unsigned bits=0;bool corner=false;
        if(d&1){for(unsigned k=0;k<16;++k)bits|=unsigned(tab[p[off[k]]]&1)<<k;corner=nine(bits);}
        if(!corner && (d&2)){bits=0;for(unsigned k=0;k<16;++k)bits|=unsigned((tab[p[off[k]]]&2)!=0)<<k;corner=nine(bits);}
        if(corner){
            int value=score(p,off,threshold);s.score[y*w+x]=value;
#ifdef FRONT_SPARSE_NMS
            if(value)s.positions[positions++]=(unsigned(y)<<16)|unsigned(x);
#endif
        }
    }
    DETAIL_ADD(s,fast,fast_begin);
    auto nms_begin=DETAIL_BEGIN(s);
    unsigned n=0;
#ifdef FRONT_SPARSE_NMS
    // Preserve raster order and strict 8-neighbor suppression, visiting only FAST candidates.
    for(unsigned i=0;i<positions;++i){int x=s.positions[i]&65535u,y=s.positions[i]>>16;
        const auto *p=s.score+y*w+x;int v=*p;
        bool keep=v>p[-w-1] && v>p[-w] && v>p[-w+1] && v>p[-1] &&
                  v>p[1] && v>p[w-1] && v>p[w] && v>p[w+1];
#else
    for(int y=3;y<h-3;++y)for(int x=3;x<w-3;++x){
        int v=s.score[y*w+x];if(!v)continue;bool keep=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&v<=s.score[(y+dy)*w+x+dx])keep=false;
#endif
        if(keep){if(n==FRONT_CANDIDATES){DETAIL_ADD(s,nms,nms_begin);return FRONT_CANDIDATES;}s.candidates[n++]={float(x),float(y),float(v),0,0};}
    }
    DETAIL_ADD(s,nms,nms_begin);
    return n;
}
static void refine(const unsigned char *im,unsigned stride,int width,int height,front_point *pts,unsigned count,Scratch &s) {
    for(int y=0;y<11;++y){float q=float(y-5)/5,vy=std::exp(-q*q);for(int x=0;x<11;++x){q=float(x-5)/5;s.weights[y*11+x]=vy*std::exp(-q*q);}}
    for(unsigned p=0;p<count;++p){
        float startx=pts[p].x,starty=pts[p].y,cx=startx,cy=starty;
        for(int it=0;it<20;++it){
            float px=cx-6.f,py=cy-6.f;int ix=(int)std::floor(px),iy=(int)std::floor(py);float a=px-ix,b=py-iy;
            if(ix>=0 && ix+13<width && iy>=0 && iy+13<height) {
                a=std::max(a,.0001f);float a12=a*(1.f-b),a22=a*b,b1=1.f-b;double ratio=(1.-a)/a;
                for(int y=0;y<13;++y){const auto *row=im+(iy+y)*stride+ix;float prev=(1-a)*(b1*row[0]+b*row[stride]);
                    for(int x=0;x<13;++x){float t=a12*row[x+1]+a22*row[x+1+stride];s.patch[y*13+x]=prev+t;prev=float(t*ratio);}}
            } else {
                float a11=(1.f-a)*(1.f-b),a12=a*(1.f-b),a21=(1.f-a)*b,a22=a*b;
                for(int y=0;y<13;++y)for(int x=0;x<13;++x){
                    int xx=std::max(0,std::min(width-1,ix+x)),xx1=std::max(0,std::min(width-1,ix+x+1));
                    int yy=std::max(0,std::min(height-1,iy+y)),yy1=std::max(0,std::min(height-1,iy+y+1));
                    s.patch[y*13+x]=im[yy*stride+xx]*a11+im[yy*stride+xx1]*a12+im[yy1*stride+xx]*a21+im[yy1*stride+xx1]*a22;
                }
            }
            double aa=0,bb=0,cc=0,r1=0,r2=0;
            for(int y=0;y<11;++y)for(int x=0;x<11;++x){
                int k=(y+1)*13+x+1;double gx=s.patch[k+1]-s.patch[k-1],gy=s.patch[k+13]-s.patch[k-13],m=s.weights[y*11+x];
                double gxx=gx*gx*m,gxy=gx*gy*m,gyy=gy*gy*m;
                aa+=gxx;bb+=gxy;cc+=gyy;r1+=gxx*(x-5)+gxy*(y-5);r2+=gxy*(x-5)+gyy*(y-5);
            }
            double det=aa*cc-bb*bb;if(std::fabs(det)<=DBL_EPSILON*DBL_EPSILON)break;
            double scale=1.0/det;float nx=float(cx+cc*scale*r1-bb*scale*r2),ny=float(cy-bb*scale*r1+aa*scale*r2);
            float err=(nx-cx)*(nx-cx)+(ny-cy)*(ny-cy);cx=nx;cy=ny;
            if(cx<0 || cx>=width || cy<0 || cy>=height || err<=1e-6)break;
        }
        if(std::fabs(cx-startx)>5 || std::fabs(cy-starty)>5){cx=startx;cy=starty;}
        pts[p].x=cx;pts[p].y=cy;
    }
}
static bool stereo_id(const front_point *left,unsigned n,probe_u64 id){for(unsigned i=0;i<n;++i)if(left[i].id==id)return true;return false;}
#ifdef FRONT_IVE_ST
static unsigned ive_roi(const unsigned char *map,unsigned stride,int ox,int oy,int w,int h,Scratch &s){
    unsigned count=0;
    // Hardware retains equal-response plateaus. Select one deterministic
    // raster-first local maximum, then preserve the existing grid policy.
    for(int y=3;y<h-3;++y)for(int x=3;x<w-3;++x){
        int px=ox+x,py=oy+y;unsigned value=map[py*stride+px];if(!value)continue;
        bool keep=true;
        for(int dy=-1;dy<=1 && keep;++dy)for(int dx=-1;dx<=1;++dx){
            if(!dx && !dy)continue;unsigned other=map[(py+dy)*stride+px+dx];
            if(other>value || (other==value && (dy<0 || (!dy && dx<0)))){keep=false;break;}
        }
        if(keep && count<FRONT_CANDIDATES)s.candidates[count++]={float(x),float(y),float(value),0,0};
    }
    return count;
}
#endif
static int detect(unsigned char *base,front_control &f,const lk_tpu_level &l,const unsigned char *mask,
                       const unsigned char *rectangle_mask,front_point *pts,unsigned &count,
                       const front_point *left,unsigned left_count,Scratch &s,const front_ops *ops) {
    auto mask_begin=DETAIL_BEGIN(s);
    int cw=f.width/f.min_px_dist,ch=f.height/f.min_px_dist;
    fill(s.close,0,cw*ch);fill(s.grid,0,f.grid_x*f.grid_y);
#ifndef FRONT_LAZY_MASK
    std::memcpy(s.updated,rectangle_mask,f.width*f.height);
#endif
    unsigned kept=0;
    for(unsigned i=0;i<count;++i){
        auto p=pts[i];int x=(int)p.x,y=(int)p.y;
        if(x<10 || x>=(int)f.width-10 || y<10 || y>=(int)f.height-10)continue;
        int xc=(int)(p.x/float(f.min_px_dist)),yc=(int)(p.y/float(f.min_px_dist));
        int xg=(int)std::floor(p.x/(float(f.width)/f.grid_x)),yg=(int)std::floor(p.y/(float(f.height)/f.grid_y));
        if(xc<0 || xc>=cw || yc<0 || yc>=ch || xg<0 || xg>=(int)f.grid_x || yg<0 || yg>=(int)f.grid_y)continue;
        if(s.close[yc*cw+xc] && !stereo_id(left,left_count,p.id))continue;
        if(mask[y*f.width+x]>127)continue;
        pts[kept++]=p;s.close[yc*cw+xc]=255;if(s.grid[yg*f.grid_x+xg]<255)++s.grid[yg*f.grid_x+xg];
#ifndef FRONT_LAZY_MASK
        int d=f.min_px_dist;
        if(x-d>=0 && x+d<(int)f.width && y-d>=0 && y+d<(int)f.height)
            for(int yy=y-d;yy<=y+d;++yy)fill(s.updated+yy*f.width+x-d,255,2*d+1);
#endif
    }
    count=kept;
    DETAIL_ADD(s,mask,mask_begin);
    if((int)f.num_features-(int)count<=std::min(20,(int)(.5*f.num_features)))return 0;
#ifdef FRONT_LAZY_MASK
    // The filtering above reads only the original mask and occupancy grid.
    // Rectangle masks are needed only when we actually top up this frame.
    mask_begin=DETAIL_BEGIN(s);
    std::memcpy(s.updated,rectangle_mask,f.width*f.height);
    for(unsigned i=0;i<count;++i){int x=(int)pts[i].x,y=(int)pts[i].y,d=f.min_px_dist;
        if(x-d>=0 && x+d<(int)f.width && y-d>=0 && y+d<(int)f.height)
            for(int yy=y-d;yy<=y+d;++yy)fill(s.updated+yy*f.width+x-d,255,2*d+1);}
    DETAIL_ADD(s,mask,mask_begin);
#endif
    unsigned cell_target=f.num_features/(f.grid_x*f.grid_y)+1,cell_req=std::max(1u,cell_target/2);
    int cellw=f.width/f.grid_x,cellh=f.height/f.grid_y;auto *im=image(base,l);unsigned extracted=0;
#ifdef FRONT_IVE_ST
    const unsigned char *corner_map=nullptr;
    if(ops->corners){int ret=ops->corners(base,&l,&corner_map);if(ret)return ret;}
#else
    (void)ops;
#endif
    for(unsigned gx=0;gx<f.grid_x;++gx)for(unsigned gy=0;gy<f.grid_y;++gy){
        if(s.grid[gy*f.grid_x+gx]>=cell_req || mask[(gy*f.height/f.grid_y)*f.width+gx*f.width/f.grid_x]==255)continue;
        int ox=gx*cellw,oy=gy*cellh;
        unsigned n;
#ifdef FRONT_IVE_ST
        if(corner_map)n=ive_roi(corner_map,f.width,ox,oy,cellw,cellh,s);
        else
#endif
        n=fast_roi(im+oy*l.row_bytes+ox,l.row_bytes,cellw,cellh,f.threshold,s);
        auto sort_begin=DETAIL_BEGIN(s);
        std::sort(s.candidates,s.candidates+n,[](const front_point &a,const front_point &b){return a.response>b.response;});
        DETAIL_ADD(s,sort,sort_begin);
#ifdef FRONT_PROFILE
        s.profile->fast_candidates+=n;
#endif
        for(unsigned k=0;k<std::min(cell_target,n);++k){auto p=s.candidates[k];p.x+=ox;p.y+=oy;
            if(s.updated[(int)p.y*f.width+(int)p.x]>127)continue;
            if(extracted==FRONT_POINTS)return FRONT_POINTS+1;
            s.extracted[extracted++]=p;}
    }
    auto refine_begin=DETAIL_BEGIN(s);
    refine(im,l.row_bytes,f.width,f.height,s.extracted,extracted,s);
    DETAIL_ADD(s,refine,refine_begin);
    unsigned out=0;
    for(unsigned i=0;i<extracted;++i){auto p=s.extracted[i];int x=(int)(p.x/float(f.min_px_dist)),y=(int)(p.y/float(f.min_px_dist));
        if(x<0 || x>=cw || y<0 || y>=ch || s.close[y*cw+x])continue;
        s.close[y*cw+x]=255;s.extracted[out++]=p;
    }
    return out;
}
}
extern "C" void front_border(unsigned char *base,const lk_tpu_level *p) {
    const int width=p->width,height=p->height,stride=p->row_bytes,b=LK_TPU_BORDER;auto *im=image(base,*p);
    for(int y=0;y<height;++y){auto *row=im+y*stride;
        for(int x=1;x<=b;++x){row[-x]=row[reflect(-x,width)];row[width-1+x]=row[reflect(width-1+x,width)];}}
    auto *start=im-b;
    for(int y=1;y<=b;++y){std::memcpy(start-y*stride,start+reflect(-y,height)*stride,stride);
        std::memcpy(start+(height-1+y)*stride,start+reflect(height-1+y,height)*stride,stride);}
}
extern "C" int front_cpu_pyramid(void *memory,front_control*,front_state *state,unsigned slot,unsigned cam) {
    auto *base=(unsigned char*)memory;auto &s=*(Scratch*)(base+FRONT_SCRATCH_OFF);const int weights[5]={1,4,6,4,1};
    for(unsigned level=1;level<state->levels;++level){auto &a=state->pyramid[slot][cam][level-1];auto &b=state->pyramid[slot][cam][level];auto *src=image(base,a),*dst=image(base,b);
        for(unsigned y=0;y<b.height;++y){
            for(int k=0;k<5;++k){const auto *row=src+(int(2*y)+k-2)*int(a.row_bytes);
                for(unsigned x=0;x<b.width;++x){int v=0;for(int j=0;j<5;++j)v+=weights[j]*row[int(2*x)+j-2];s.gaussian[k][x]=v;}}
            for(unsigned x=0;x<b.width;++x){int v=0;for(int k=0;k<5;++k)v+=weights[k]*s.gaussian[k][x];dst[y*b.row_bytes+x]=(v+128)>>8;}
        }
        front_border(base,&b);
    }
    return 0;
}
extern "C" int front_run(void *memory,const front_ops *ops) {
    auto *base=(unsigned char*)memory;auto &f=*(front_control*)(base+FRONT_META_OFF);auto &state=*(front_state*)(base+FRONT_STATE_OFF);auto &s=*(Scratch*)(base+FRONT_SCRATCH_OFF);
    if(f.magic!=FRONT_MAGIC || f.version!=FRONT_VERSION || f.width!=640 || (f.height!=400 && f.height!=480) || f.histogram>1 || !f.num_features || f.num_features>120 || !f.grid_x || !f.grid_y || f.grid_x>FRONT_GRID_MAX || f.grid_y>FRONT_GRID_MAX || f.grid_x*f.grid_y>f.num_features || f.min_px_dist<4 || f.min_px_dist>128 || f.threshold>255 || f.input_count[0]>FRONT_POINTS || f.input_count[1]>FRONT_POINTS)return -40;
    auto *detail=front_detail_at(memory);fill(detail,0,sizeof(*detail));
    detail->magic=FRONT_DETAIL_MAGIC;s.profile=detail;s.clock=ops->clock;
#ifdef FRONT_PROFILE
    detail->flags|=1u;
#endif
#ifdef FRONT_SPARSE_NMS
    detail->flags|=2u;
#endif
#ifdef FRONT_LAZY_MASK
    detail->flags|=4u;
#endif
#ifdef FRONT_TPU_BASE_CACHE
    detail->flags|=32u;
#endif
    probe_u64 begin=ops->clock();f.equalize_ticks=f.pyramid_ticks=f.detect_ticks=f.lk_ticks=0;
    if(state.magic!=FRONT_STATE_MAGIC || state.width!=f.width || state.height!=f.height || state.histogram!=f.histogram){
        fill(&state,0,sizeof(state));state.magic=FRONT_STATE_MAGIC;state.width=f.width;state.height=f.height;state.histogram=f.histogram;state.levels=LK_TPU_LEVELS;
        for(unsigned slot=0;slot<2;++slot)for(unsigned cam=0;cam<2;++cam){
            unsigned offset=front_image_layout(state.pyramid[slot][cam],FRONT_PYR_OFF+(slot*2+cam)*FRONT_CAM_BYTES,f.width,f.height);
            if(offset>FRONT_PYR_OFF+(slot*2+cam+1)*FRONT_CAM_BYTES)return -41;
        }
    }
    f.levels=state.levels;int old=-1;
    for(unsigned i=0;i<2;++i)if(state.valid[i] && state.timestamp[i]==f.previous_timestamp)old=i;
    if(old<0 && (f.input_count[0] || f.input_count[1]))return -42;
    unsigned current=old<0?0:1-old;
    for(unsigned cam=0;cam<2;++cam){
        auto t=ops->clock();auto &l=state.pyramid[current][cam][0];
        unsigned char lut[256];
#ifdef FRONT_IVE_HIST
        if(f.histogram && ops->histogram){int ret=ops->histogram(base,cam,s.histogram);if(ret)return ret;lut_from_histogram(s.histogram,f.width*f.height,lut);}
        else
#endif
        prepare_lut(base+FRONT_RAW_OFF+cam*FRONT_PIXELS,f.width*f.height,f.histogram!=0,lut);
        DETAIL_ADD(s,histogram,t);
        auto lut_begin=DETAIL_BEGIN(s);
        if(ops->equalize){int ret=ops->equalize(base,&f,&state,current,cam,lut);if(ret)return ret;}
        else apply_lut(image(base,l),base+FRONT_RAW_OFF+cam*FRONT_PIXELS,f.width,f.height,l.row_bytes,lut);
        DETAIL_ADD(s,lut,lut_begin);
        auto border_begin=DETAIL_BEGIN(s);
        front_border(base,&l);
        DETAIL_ADD(s,border,border_begin);f.equalize_ticks+=ops->clock()-t;
        t=ops->clock();int ret=ops->pyramid(base,&f,&state,current,cam);if(ret)return ret;f.pyramid_ticks+=ops->clock()-t;
        std::memcpy(base+FRONT_MASK_OFF+(current*2+cam)*FRONT_PIXELS,base+FRONT_INPUT_MASK_OFF+cam*FRONT_PIXELS,f.width*f.height);
    }
    state.valid[current]=1;state.timestamp[current]=f.timestamp;
    f.snapshot_wait_ticks=0;
    if(f.deferred_snapshot){
        auto t=ops->clock();
        if(!ops->snapshot)return -45;
        int ret=ops->snapshot(memory,&f);if(ret)return ret;
        f.snapshot_wait_ticks=ops->clock()-t;
        if(f.input_count[0]>FRONT_POINTS || f.input_count[1]>FRONT_POINTS || !f.num_features || f.num_features>120 || f.grid_x*f.grid_y>f.num_features)return -46;
        if(old<0 && (f.input_count[0] || f.input_count[1]))return -42;
    }
    f.reset=!(f.input_count[0] || f.input_count[1]);unsigned source=f.reset?current:(unsigned)old;
    for(unsigned cam=0;cam<2;++cam){f.count[cam]=f.input_count[cam];std::memcpy(f.old[cam],f.input[cam],f.count[cam]*sizeof(front_point));}
    const unsigned char *mask0=base+FRONT_MASK_OFF+source*2*FRONT_PIXELS,*mask1=mask0+FRONT_PIXELS;
    auto t=ops->clock();int found=detect(base,f,state.pyramid[source][0][0],mask0,mask0,f.old[0],f.count[0],nullptr,0,s,ops);
    if(found<0)return found;unsigned n=found;
    if(n>FRONT_POINTS || f.count[0]+n>FRONT_POINTS)return -43;
    f.detect_ticks+=ops->clock()-t;
    if(n){
        t=ops->clock();int ret=ops->flow(base,state.pyramid[source][0],state.pyramid[source][1],state.levels,s.extracted,n,f.output[0],f.valid[0]);if(ret)return ret;f.lk_ticks+=ops->clock()-t;
        for(unsigned i=0;i<n;++i){auto p=s.extracted[i];
            if((int)p.x<0 || (int)p.y<0 || (int)p.x>=(int)f.width || (int)p.y>=(int)f.height)continue;
            p.id=++f.currid;f.old[0][f.count[0]++]=p;
            auto q=f.output[0][i];if(f.valid[0][i] && (int)q.x>=0 && (int)q.y>=0 && (int)q.x<(int)f.width && (int)q.y<(int)f.height){
                if(f.count[1]==FRONT_POINTS)return -44;
                p.x=q.x;p.y=q.y;f.old[1][f.count[1]++]=p;}}
    }
    t=ops->clock();found=detect(base,f,state.pyramid[source][1][0],mask1,mask0,f.old[1],f.count[1],f.old[0],f.count[0],s,ops);
    if(found<0)return found;n=found;
    if(n>FRONT_POINTS || f.count[1]+n>FRONT_POINTS)return -43;
    for(unsigned i=0;i<n;++i){auto p=s.extracted[i];p.id=++f.currid;f.old[1][f.count[1]++]=p;}f.detect_ticks+=ops->clock()-t;
    for(unsigned cam=0;cam<2;++cam){
        fill(f.valid[cam],0,f.count[cam]);for(unsigned i=0;i<f.count[cam];++i)f.output[cam][i]={f.old[cam][i].x,f.old[cam][i].y};
        if(!f.reset && f.count[cam]>=10){t=ops->clock();int ret=ops->flow(base,state.pyramid[source][cam],state.pyramid[current][cam],state.levels,f.old[cam],f.count[cam],f.output[cam],f.valid[cam]);if(ret)return ret;f.lk_ticks+=ops->clock()-t;}
    }
    f.total_ticks=ops->clock()-begin;return 0;
}

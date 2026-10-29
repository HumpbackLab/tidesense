/* Included after the low-level TPU submission code. No A53 iteration callbacks. */
#include <math.h>
#include "lk_shared.h"

static int lk_tpu_roi(uintptr_t base, struct lk_tpu_control *m, unsigned level,
                     unsigned lane, float x, float y, unsigned job)
{
    struct lk_tpu_level *l=&m->level[level];
    int ix=(int)floorf(x-7.5f), iy=(int)floorf(y-7.0f);
    int valid=ix>=-16 && iy>=-15 && ix<(int)l->width && iy<(int)l->height;
    if(!valid) {x=l->width*0.5f;y=l->height*0.5f;ix=(int)floorf(x-7.5f);iy=(int)floorf(y-7.0f);}
    float fx=x-7.5f-ix, fy=y-7.0f-iy;
    unsigned short *weights=(void *)(base+LK_TPU_WEIGHTS_OFF+lane*8);
    weights[0]=lk_tpu_bf16((1-fx)*(1-fy));weights[1]=lk_tpu_bf16(fx*(1-fy));
    weights[2]=lk_tpu_bf16((1-fx)*fy);weights[3]=lk_tpu_bf16(fx*fy);
    unsigned off=job?LK_TPU_ITER_DMA_OFF:LK_TPU_INIT_DMA_OFF;
    {
        unsigned p=job?1u:0;
        unsigned desc=job?m->iter_patch[lane]:m->init_patch[0][lane];
        uint32_t *td=(void *)(base+off+desc);
        td[12]=base+l->planes[p]+(iy+LK_TPU_BORDER-(job?0:1))*l->row_bytes+(ix+LK_TPU_BORDER-(job?0:1));
        td[6]=(td[6]&0xffff0000u)|l->row_bytes;
    }
    if(!job) {
        unsigned short *rows=(void *)(base+LK_TPU_ROW_MASK_OFF+lane*(LK_TPU_H+1)*2);
        unsigned short *cols=(void *)(base+LK_TPU_COL_MASK_OFF+lane*(LK_TPU_W+1)*2);
        for(unsigned k=0;k<LK_TPU_H+1;++k)rows[k]=(iy+(int)k>=0 && iy+k<l->height)?0x3f80:0;
        for(unsigned k=0;k<LK_TPU_W+1;++k)cols[k]=(ix+(int)k>=0 && ix+k<l->width)?0x3f80:0;
    }
    return valid;
}
static int lk_tpu_submit(uintptr_t base, struct tpu_probe_control *ctl,
                         struct lk_tpu_control *m, unsigned job)
{
    unsigned off=job?LK_TPU_ITER_DMA_OFF:LK_TPU_INIT_DMA_OFF;
    uint64_t t=probe_clock();
    clean_dcache_range(base+LK_TPU_WEIGHTS_OFF,LK_TPU_BATCH*8);
    if(!job)clean_dcache_range(base+LK_TPU_ROW_MASK_OFF,4096);
    clean_dcache_range(base+off,m->dma_bytes[job]);
    m->prepare_ticks+=probe_clock()-t;
    ctl->dmabuf_bytes=m->dma_bytes[job];
    t=probe_clock();
    int ret=probe_execute_at(ctl,base,off);
    if(ret)probe_resync();
    uint64_t finished=probe_clock();
    m->execute_ticks+=finished-t;m->submissions++;
    if(m->trace_flags==LK_TPU_TRACE_MAGIC) {
        if(m->trace_count<LK_TPU_TRACE_MAX) {
            struct lk_tpu_trace_span *spans=(void *)(base+LK_TPU_TRACE_OFF);
            spans[m->trace_count++]=(struct lk_tpu_trace_span){t,finished};
        } else m->trace_overflow=1;
    }
    inv_dcache_range(base+LK_TPU_HESS_OFF,512);
    return ret;
}
static void c906_tpu_lk_impl(uintptr_t base, uint64_t received, unsigned available_bytes, int invalidate)
{
    struct tpu_probe_control *ctl=(void *)base;
    struct lk_tpu_control *m=(void *)(base+LK_TPU_META_OFF);
    if(invalidate)inv_dcache_range(base,LK_TPU_BYTES);
    ctl->status=-20;ctl->completed=0;
    m->trace_magic=0;m->trace_count=0;m->trace_overflow=0;m->trace_begin=received;
    if(base>0x8ff40000u-LK_TPU_BYTES || m->version!=LK_TPU_VERSION ||
       m->batch!=LK_TPU_BATCH ||
       !m->count || m->count>LK_TPU_POINTS || !m->levels || m->levels>LK_TPU_LEVELS ||
       !m->max_iterations || m->max_iterations>30)goto done;
    for(unsigned j=0;j<2;++j) {
        if(m->dma_bytes[j]<128 || m->dma_bytes[j]>40000)goto done;
        for(unsigned p=0;p<1u;++p)for(unsigned k=0;k<LK_TPU_BATCH;++k) {
            unsigned d=j?m->iter_patch[k]:m->init_patch[p][k];
            if((d&63u) || d+64>m->dma_bytes[j])goto done;
        }
    }
    for(unsigned l=0;l<m->levels;++l) {
        struct lk_tpu_level *v=&m->level[l];
        if(v->width<16 || v->width>640 || v->height<15 || v->height>480 ||
           v->row_bytes!=v->width+2*LK_TPU_BORDER)goto done;
        for(unsigned p=0;p<2;++p)
            if(v->planes[p]<LK_TPU_PLANES_OFF ||
               v->planes[p]>available_bytes-(v->height+2*LK_TPU_BORDER)*v->row_bytes)goto done;
    }
    for(unsigned p=0;p<m->count;++p) {
        if(!isfinite(m->input[p].x) || !isfinite(m->input[p].y) ||
           !isfinite(m->initial[p].x) || !isfinite(m->initial[p].y) ||
           fabsf(m->input[p].x)>100000 || fabsf(m->input[p].y)>100000 ||
           fabsf(m->initial[p].x)>100000 || fabsf(m->initial[p].y)>100000)goto done;
    }
    uint64_t begin=probe_clock();
    m->prepare_ticks=0;m->execute_ticks=0;m->solve_ticks=0;m->submissions=0;m->valid_count=0;
    for(unsigned first=0;first<m->count;first+=LK_TPU_BATCH) {
        float nx[LK_TPU_BATCH],ny[LK_TPU_BATCH],hxx[LK_TPU_BATCH],hxy[LK_TPU_BATCH],hyy[LK_TPU_BATCH];
        float prevdx[LK_TPU_BATCH],prevdy[LK_TPU_BATCH];
        unsigned char active[LK_TPU_BATCH];
        unsigned count=m->count-first;if(count>LK_TPU_BATCH)count=LK_TPU_BATCH;
        for(unsigned k=0;k<count;++k) {
            nx[k]=m->initial[first+k].x/(1u<<(m->levels-1));
            ny[k]=m->initial[first+k].y/(1u<<(m->levels-1));
            m->valid[first+k]=1;m->iterations[first+k]=0;
        }
        for(int level=(int)m->levels-1;level>=0;--level) {
            uint64_t t=probe_clock();
            for(unsigned k=0;k<LK_TPU_BATCH;++k) {
                float x=k<count?m->input[first+k].x/(1u<<level):0;
                float y=k<count?m->input[first+k].y/(1u<<level):0;
                active[k]=lk_tpu_roi(base,m,level,k,x,y,0) && k<count;
                if(k<count && level!=(int)m->levels-1){nx[k]*=2;ny[k]*=2;}
            }
            m->prepare_ticks+=probe_clock()-t;
            if(probe_clock()-begin>12500000u){ctl->status=-21;goto done;}
            int ret=lk_tpu_submit(base,ctl,m,0);
            if(ret){ctl->status=ret;goto done;}
            unsigned short *h=(void *)(base+LK_TPU_HESS_OFF);
            t=probe_clock();
            for(unsigned k=0;k<count;++k) {
                hxx[k]=lk_tpu_float(h[k]);hxy[k]=lk_tpu_float(h[LK_TPU_BATCH+k]);
                hyy[k]=lk_tpu_float(h[2*LK_TPU_BATCH+k]);prevdx[k]=prevdy[k]=0;
                float a=hxx[k],b=hxy[k],c=hyy[k];
                float eig=(a+c-sqrtf((a-c)*(a-c)+4*b*b))/(2*LK_TPU_PIXELS);
                /* Scharr/32 makes OpenCV's 1e-4 eigenvalue threshold 1024x larger. */
                if(a*c-b*b<1e-7f || eig<0.1024f)active[k]=0;
                if(!active[k] && !level)m->valid[first+k]=0;
            }
            m->solve_ticks+=probe_clock()-t;
            for(unsigned it=0;it<m->max_iterations;++it) {
                t=probe_clock();unsigned any=0;
                for(unsigned k=0;k<count;++k)if(active[k]) {
                    int valid=lk_tpu_roi(base,m,level,k,nx[k],ny[k],1);
                    if(!valid){active[k]=0;if(!level)m->valid[first+k]=0;}
                    any+=active[k];
                }
                m->prepare_ticks+=probe_clock()-t;
                if(!any)break;
                if(probe_clock()-begin>12500000u){ctl->status=-21;goto done;}
                ret=lk_tpu_submit(base,ctl,m,1);if(ret){ctl->status=ret;goto done;}
                unsigned short *rhs=(void *)(base+LK_TPU_RHS_OFF);
                t=probe_clock();
                for(unsigned k=0;k<count;++k)if(active[k]) {
                    float bx=lk_tpu_float(rhs[k]),by=lk_tpu_float(rhs[LK_TPU_BATCH+k]);
                    float det=hxx[k]*hyy[k]-hxy[k]*hxy[k];
                    float dx=(hyy[k]*bx-hxy[k]*by)/det,dy=(hxx[k]*by-hxy[k]*bx)/det;
                    if(!isfinite(dx) || !isfinite(dy)){active[k]=0;if(!level)m->valid[first+k]=0;continue;}
                    nx[k]+=dx;ny[k]+=dy;m->iterations[first+k]++;
                    if(dx*dx+dy*dy<=0.0001f)active[k]=0;
                    else if(it && fabsf(dx+prevdx[k])<0.01f && fabsf(dy+prevdy[k])<0.01f) {
                        nx[k]-=dx*0.5f;ny[k]-=dy*0.5f;active[k]=0;
                    }
                    prevdx[k]=dx;prevdy[k]=dy;
                }
                m->solve_ticks+=probe_clock()-t;
            }
        }
        for(unsigned k=0;k<count;++k) {
            m->output[first+k].x=nx[k];m->output[first+k].y=ny[k];
            m->valid_count+=m->valid[first+k]!=0;
        }
    }
    m->total_ticks=probe_clock()-begin;ctl->status=0;ctl->completed=1;
done:
    if(m->trace_flags==LK_TPU_TRACE_MAGIC) {
        m->trace_end=probe_clock();m->trace_magic=LK_TPU_TRACE_MAGIC;
        clean_dcache_range(base+LK_TPU_TRACE_OFF,m->trace_count*sizeof(struct lk_tpu_trace_span));
    }
    clean_dcache_range(base,TPU_PROBE_CONTROL_BYTES);
}

static void c906_tpu_lk(uintptr_t base,uint64_t received)
{c906_tpu_lk_impl(base,received,LK_TPU_BYTES,1);}

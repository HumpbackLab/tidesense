/* A53 command preparation only. Iterations are executed by C906 and TPU. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cvikernel/cvikernel.h"
#ifdef OPENVINS_C906_FRONTEND
#include "openvins_frontend_config.h"
#endif
#ifndef LK_TPU_ROI_CACHE
#define LK_TPU_ROI_CACHE 0
#endif
#ifndef LK_TPU_DUAL_BATCH
#define LK_TPU_DUAL_BATCH 0
#endif
#include "lk_shared.h"
extern int cvi181x_dmabuf_size(uint8_t *, size_t, size_t *, size_t *);
extern int cvi181x_dmabuf_convert(uint8_t *, size_t, uint8_t *);
extern int cvi181x_dmabuf_relocate(uint8_t *, uint64_t, uint32_t, uint32_t);

static void load_fmt(cvk_context_t *c, cvk_tl_t *l, uint64_t address, unsigned layer, cvk_fmt_t fmt)
{
    cvk_tg_t g = {.fmt=fmt, .start_address=address,
        .shape={l->shape.n,l->shape.c,l->shape.h,l->shape.w}};
    g.stride = c->ops->tg_default_stride(c,g.shape,g.fmt);
    cvk_tdma_g2l_tensor_copy_param_t p = {.src=&g,.dst=l,.layer_id=layer};
    c->ops->set_layer_id(c,layer);
    c->ops->tdma_g2l_bf16_tensor_copy(c,&p);
    c->ops->set_layer_id(c,0);
}
static void load(cvk_context_t *c, cvk_tl_t *l, uint64_t address, unsigned layer)
{ load_fmt(c,l,address,layer,CVK_FMT_BF16); }
static void store(cvk_context_t *c, cvk_tl_t *l, uint64_t address)
{
    cvk_tg_t g = {.fmt=CVK_FMT_BF16, .start_address=address,
        .shape={l->shape.n,l->shape.c,l->shape.h,l->shape.w}};
    g.stride = c->ops->tg_default_stride(c,g.shape,g.fmt);
    cvk_tdma_l2g_tensor_copy_param_t p = {.src=l,.dst=&g};
    c->ops->tdma_l2g_bf16_tensor_copy(c,&p);
}
static void mul(cvk_context_t *c, cvk_tl_t *r, cvk_tl_t *a, cvk_tl_t *b)
{
    cvk_tiu_mul_param_t p = {.res_low=r,.a=a,.b=b}; c->ops->tiu_mul(c,&p);
}
static void interpolate(cvk_context_t *c, cvk_tl_t *raw, cvk_tl_t *weights,
                        cvk_tl_t *out)
{
    for(unsigned k=0;k<4;++k) {
        cvk_tl_t a=*raw, b=*weights;
        a.shape=out->shape;
        a.start_address+=(k/2)*raw->stride.h+(k%2)*2;
        b.shape=out->shape; b.start_address+=k*2;
        b.stride.h=0; b.stride.w=0;
        if(!k) mul(c,out,&a,&b);
        else {
            cvk_tiu_mac_param_t p={.res_low=out,.a=&a,.b=&b};
            c->ops->tiu_mac(c,&p);
        }
    }
}
static void gather(cvk_context_t *c, cvk_tl_t *raw, uint64_t addr, unsigned tag)
{
    for(unsigned i=0;i<LK_TPU_BATCH;++i) {
        cvk_tl_t one=*raw;one.shape.c=1;
        one.start_address+=(i%8)*c->info.lmem_size+(i/8)*raw->stride.c;
        load_fmt(c,&one,addr,tag+i,CVK_FMT_U8);
    }
}
static void center(cvk_context_t *c,cvk_tl_t *a)
{
    cvk_tiu_add_param_t p={.res_low=a,.a_low=a,.b_is_const=1,
        .b_const={.val=(int16_t)0xc300,.is_signed=1}}; /* -128 */
    c->ops->tiu_add(c,&p);
}
#if LK_TPU_ROI_CACHE
static void center_copy(cvk_context_t *c,cvk_tl_t *src,cvk_tl_t *dst)
{
    cvk_tiu_add_param_t p={.res_low=dst,.a_low=src,.b_is_const=1,
        .b_const={.val=(int16_t)0xc300,.is_signed=1}};
    c->ops->tiu_add(c,&p);
}
#endif
static void scharr(cvk_context_t *c,cvk_tl_t *image,cvk_tl_t *out,
                    cvk_tl_t *kernel,cvk_tl_t *rows,cvk_tl_t *cols)
{
    cvk_tiu_depthwise_pt_convolution_param_t p={.ifmap=image,.ofmap=out,.weight=kernel,
        .stride_h=1,.stride_w=1,.dilation_h=1,.dilation_w=1};
    c->ops->tiu_pt_depthwise_convolution(c,&p);
    cvk_tl_t r=*rows,k=*cols;
    r.shape=k.shape=out->shape;r.stride.w=0;k.stride.h=0;
    mul(c,out,out,&r);mul(c,out,out,&k);
}
static void reduce(cvk_context_t *c, cvk_tl_t *product, cvk_tl_t *sum,
                   cvk_tl_t *a, cvk_tl_t *b, uint64_t dst)
{
    mul(c,product,a,b);
    cvk_tiu_average_pooling_param_t p={.ifmap=product,.ofmap=sum,
        .kh=LK_TPU_H,.kw=LK_TPU_W,.stride_h=1,.stride_w=1,
        .avg_pooling_const=0x4370}; /* 240 / kernel area = 1, hence sum */
    c->ops->tiu_average_pooling(c,&p);store(c,sum,dst);
}

#include "lk_dual_jobs.h"

int lk_tpu_make_jobs(uint8_t *shared, uint64_t phys)
{
    struct lk_tpu_control *m=(void *)(shared+LK_TPU_META_OFF);
    cvk_tl_shape_t shape={1,LK_TPU_BATCH,LK_TPU_H,LK_TPU_W};
    uint8_t *storage=calloc(1,131072);
    cvk_reg_info_t reg={.chip_ver_str="cv181x",.cmdbuf_size=131072,.cmdbuf=storage};
    cvk_context_t *c=storage?cvikernel_register(&reg):NULL;
    cvk_tl_t *allocated[14]; unsigned count=0; int ret=-1;
    if(!c) goto done;
    m->dual_magic=0;
    m->cached_magic=m->cached_dma_bytes=m->cache_hits=m->cache_misses=0;
#if LK_TPU_ROI_CACHE
    const char *cache_env=getenv("OPENVINS_TPU_LK_ROI_CACHE");
    const int use_cache=!cache_env || strcmp(cache_env,"0");
#else
    const int use_cache=0;
#endif
#define ALLOC(var,sh) cvk_tl_t *var=c->ops->lmem_alloc_tensor(c,(sh),CVK_FMT_BF16,1); \
    if(!var){goto done;} allocated[count++]=var
    cvk_tl_shape_t rawshape={1,LK_TPU_BATCH,LK_TPU_H+1,LK_TPU_W+1};
    cvk_tl_shape_t weightshape={1,LK_TPU_BATCH,1,4},sumshape={1,LK_TPU_BATCH,1,1};
    cvk_tl_shape_t imageshape={1,LK_TPU_BATCH,LK_TPU_H+3,LK_TPU_W+3};
    cvk_tl_shape_t kernelshape={1,LK_TPU_BATCH,3,3};
    cvk_tl_shape_t rowshape={1,LK_TPU_BATCH,LK_TPU_H+1,1},colshape={1,LK_TPU_BATCH,1,LK_TPU_W+1};
    ALLOC(raw,rawshape);ALLOC(weights,weightshape);ALLOC(tmpl,shape);
    ALLOC(gx,shape);ALLOC(gy,shape);ALLOC(sample,shape);ALLOC(product,shape);ALLOC(sum,sumshape);
    ALLOC(image,imageshape);ALLOC(kernelx,kernelshape);ALLOC(kernely,kernelshape);
    ALLOC(rows,rowshape);ALLOC(cols,colshape);
#if LK_TPU_ROI_CACHE
    // Keep an unmodified current-image window. Centering is out of place, so
    // a cache hit cannot subtract 128 from an already centered window.
    ALLOC(current_cache,rawshape);
#endif
    const float kx[9]={-3,0,3,-10,0,10,-3,0,3},ky[9]={-3,-10,-3,0,0,0,3,10,3};
    for(unsigned ch=0;ch<LK_TPU_BATCH;++ch)for(unsigned k=0;k<9;++k) {
        ((uint16_t *)(shared+LK_TPU_KERNEL_X_OFF))[ch*9+k]=lk_tpu_bf16(kx[k]/32);
        ((uint16_t *)(shared+LK_TPU_KERNEL_Y_OFF))[ch*9+k]=lk_tpu_bf16(ky[k]/32);
    }
    for(unsigned job=0;job<(use_cache?3u:2u);++job) {
        load(c,weights,phys+LK_TPU_WEIGHTS_OFF,0);
        if(job==0) {
            load(c,kernelx,phys+LK_TPU_KERNEL_X_OFF,0);load(c,kernely,phys+LK_TPU_KERNEL_Y_OFF,0);
            load(c,rows,phys+LK_TPU_ROW_MASK_OFF,0);load(c,cols,phys+LK_TPU_COL_MASK_OFF,0);
            gather(c,image,phys+LK_TPU_PLANES_OFF,0x1000);center(c,image);
            scharr(c,image,raw,kernelx,rows,cols);interpolate(c,raw,weights,gx);
            scharr(c,image,raw,kernely,rows,cols);interpolate(c,raw,weights,gy);
            cvk_tl_t gray=*image;
            gray.start_address+=image->stride.h+2;
            interpolate(c,&gray,weights,tmpl);
            reduce(c,product,sum,gx,gx,phys+LK_TPU_HESS_OFF);
            reduce(c,product,sum,gx,gy,phys+LK_TPU_HESS_OFF+LK_TPU_BATCH*2);
            reduce(c,product,sum,gy,gy,phys+LK_TPU_HESS_OFF+LK_TPU_BATCH*4);
        } else {
#if LK_TPU_ROI_CACHE
            if(use_cache){
                if(job==1)gather(c,current_cache,phys+LK_TPU_PLANES_OFF,0x2000);
                center_copy(c,current_cache,raw);
            } else
#endif
            {
                gather(c,raw,phys+LK_TPU_PLANES_OFF,0x2000);
                center(c,raw);
            }
            interpolate(c,raw,weights,sample);
            cvk_tiu_sub_param_t sub={.res_low=sample,.a_low=tmpl,.b_low=sample};
            c->ops->tiu_sub(c,&sub);
            reduce(c,product,sum,gx,sample,phys+LK_TPU_RHS_OFF);
            reduce(c,product,sum,gy,sample,phys+LK_TPU_RHS_OFF+LK_TPU_BATCH*2);
        }
        uint32_t cmdbytes=0;size_t dmabytes=0,pmu=0;
        uint8_t *commands=c->ops->acquire_cmdbuf(c,&cmdbytes);
        unsigned off=job==0?LK_TPU_INIT_DMA_OFF:job==1?LK_TPU_ITER_DMA_OFF:LK_TPU_CACHED_DMA_OFF;
        unsigned limit=job==2?LK_TPU_WEIGHTS_OFF-LK_TPU_CACHED_DMA_OFF:
                       use_cache && job==1?LK_TPU_CACHED_DMA_OFF-LK_TPU_ITER_DMA_OFF:40000u;
        if(!commands || cvi181x_dmabuf_size(commands,cmdbytes,&dmabytes,&pmu) ||
           dmabytes>limit || cvi181x_dmabuf_convert(commands,cmdbytes,shared+off) ||
           cvi181x_dmabuf_relocate(shared+off,phys+off,dmabytes,0)) goto done;
        if(job==2)m->cached_dma_bytes=dmabytes;else m->dma_bytes[job]=dmabytes;
        const struct tpu_probe_dma_header *h=(void *)(shared+off);
        const struct tpu_probe_cpu_desc *d=(const void *)(h+1);
        unsigned found=0;
        for(unsigned s=0;s<h->cpu_count;++s,++d) {
            uint32_t *td=(void *)(shared+((uint64_t)d->offset_tdma<<6)-phys);
            for(unsigned i=0;i<(d->num_tdma&0xffffu);++i,td+=16) {
                unsigned tag=td[15]>>16;
                unsigned rel=(uint8_t *)td-(shared+off);
                if(!job && tag>=0x1000 && tag<0x1000+LK_TPU_BATCH) {
                    m->init_patch[(tag-0x1000)/LK_TPU_BATCH][(tag-0x1000)%LK_TPU_BATCH]=rel;found++;
                } else if(job==1 && tag>=0x2000 && tag<0x2000+LK_TPU_BATCH) {
                    m->iter_patch[tag-0x2000]=rel;found++;
                }
            }
        }
        printf("LK job=%u dma_bytes=%zu tiu=%u tdma=%u segments=%u patches=%u\n",
               job,dmabytes,h->tiu_count,h->tdma_count,h->cpu_count,found);
        if(found!=(job==2?0u:LK_TPU_BATCH))goto done;
        c->ops->reset(c);
    }
    if(use_cache)m->cached_magic=LK_TPU_CACHE_MAGIC;
    ret=0;
done:
    if(c) {while(count)c->ops->lmem_free_tensor(c,allocated[--count]);c->ops->cleanup(c);free(c);}
    free(storage);
#if LK_TPU_DUAL_BATCH
    if(!ret)ret=lk_tpu_make_dual_jobs(shared,phys);
#endif
    return ret;
}

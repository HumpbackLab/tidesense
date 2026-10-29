/* Included after LK firmware. Only C906 submits TPU commands. */
#include "front_shared.h"
static int front_equalize_tpu(void *memory,struct front_control *f,struct front_state *s,unsigned slot,unsigned cam,const unsigned char *lut) {
    uintptr_t base=(uintptr_t)memory;struct tpu_probe_control *ctl=memory;struct lk_tpu_level *l=&s->pyramid[slot][cam][0];
    unsigned camoff=FRONT_PYR_OFF+(slot*2+cam)*FRONT_CAM_BYTES;
    if(f->equalize_dma_bytes<128 || f->equalize_dma_bytes>131072 || f->equalize_patch_count>128)return -54;
    for(unsigned ch=0;ch<8;++ch)memcpy((void*)(base+FRONT_LUT_OFF+ch*256),lut,256);
    for(unsigned i=0;i<f->equalize_patch_count;++i){struct front_patch *p=&f->equalize_patch[i];
        if(p->offset+64>f->equalize_dma_bytes || (p->offset&63) || (p->word!=11 && p->word!=12) || p->relative>=FRONT_CAM_BYTES)return -55;
        ((unsigned*)(base+FRONT_EQ_DMA_OFF+p->offset))[p->word]=base+p->relative+(p->word==12?FRONT_RAW_OFF+cam*FRONT_PIXELS:camoff);
    }
    // The destination may still have dirty cache lines from an older slot.
    flush_dcache_range(base+l->planes[0],(l->height+2*LK_TPU_BORDER)*l->row_bytes);
    clean_dcache_range(base+FRONT_LUT_OFF,2048);clean_dcache_range(base+FRONT_EQ_DMA_OFF,f->equalize_dma_bytes);
    ctl->dmabuf_bytes=f->equalize_dma_bytes;int ret=probe_execute_at(ctl,base,FRONT_EQ_DMA_OFF);
    inv_dcache_range(base+l->planes[0],(l->height+2*LK_TPU_BORDER)*l->row_bytes);return ret;
}
static int front_pyramid_tpu(void *memory,struct front_control *f,struct front_state *s,unsigned slot,unsigned cam) {
    uintptr_t base=(uintptr_t)memory;struct tpu_probe_control *ctl=memory;
    unsigned camoff=FRONT_PYR_OFF+(slot*2+cam)*FRONT_CAM_BYTES;
    for(unsigned level=1;level<s->levels;++level){
        struct lk_tpu_level *a=&s->pyramid[slot][cam][level-1],*b=&s->pyramid[slot][cam][level];
        clean_dcache_range(base+a->planes[0],(a->height+2*LK_TPU_BORDER)*a->row_bytes);
        unsigned off=FRONT_PYR_DMA_OFF+(level-1)*FRONT_PYR_JOB_BYTES,bytes=f->pyramid_dma_bytes[level-1];
        if(bytes<128 || bytes>FRONT_PYR_JOB_BYTES || f->pyramid_patch_count[level-1]>64)return -51;
        for(unsigned i=0;i<f->pyramid_patch_count[level-1];++i){struct front_patch *p=&f->pyramid_patch[level-1][i];
            if(p->offset+64>bytes || (p->offset&63) || (p->word!=11 && p->word!=12) || p->relative>=FRONT_CAM_BYTES)return -52;
            ((unsigned*)(base+off+p->offset))[p->word]=base+camoff+p->relative;
        }
        flush_dcache_range(base+b->planes[0],(b->height+2*LK_TPU_BORDER)*b->row_bytes);
        ctl->dmabuf_bytes=bytes;clean_dcache_range(base+FRONT_WEIGHTS_OFF,256);clean_dcache_range(base+off,bytes);
        int ret=probe_execute_at(ctl,base,off);if(ret)return ret;
        inv_dcache_range(base+b->planes[0],(b->height+2*LK_TPU_BORDER)*b->row_bytes);
        front_border(memory,b);
    }
    return 0;
}
static int front_flow_tpu(void *memory,const struct lk_tpu_level *a,const struct lk_tpu_level *b,
                         unsigned levels,const struct front_point *points,unsigned count,
                         struct lk_tpu_point *output,unsigned char *valid) {
    uintptr_t base=(uintptr_t)memory;struct lk_tpu_control *m=(void*)(base+LK_TPU_META_OFF);
    struct tpu_probe_control *ctl=memory;
    m->version=LK_TPU_VERSION;m->levels=levels;m->batch=LK_TPU_BATCH;m->max_iterations=30;m->trace_flags=0;
    for(unsigned l=0;l<levels;++l){m->level[l]=a[l];m->level[l].planes[1]=b[l].planes[0];}
    // Gaussian outputs and reflected halos are C906-owned; publish once per flow.
    for(unsigned l=0;l<levels;++l){
        clean_dcache_range(base+a[l].planes[0],(a[l].height+2*LK_TPU_BORDER)*a[l].row_bytes);
        clean_dcache_range(base+b[l].planes[0],(b[l].height+2*LK_TPU_BORDER)*b[l].row_bytes);
    }
    for(unsigned first=0;first<count;first+=LK_TPU_POINTS){m->count=count-first;if(m->count>LK_TPU_POINTS)m->count=LK_TPU_POINTS;
        for(unsigned i=0;i<m->count;++i)m->input[i]=m->initial[i]=(struct lk_tpu_point){points[first+i].x,points[first+i].y};
        c906_tpu_lk_impl(base,probe_clock(),FRONT_BYTES,0);
        if(ctl->status || ctl->completed!=1)return ctl->status?ctl->status:-53;
        for(unsigned i=0;i<m->count;++i){output[first+i]=m->output[i];valid[first+i]=m->valid[i];}
    }
    return 0;
}
static int front_wait_snapshot(void *memory,struct front_control *f) {
    uintptr_t base=(uintptr_t)memory;
    volatile struct front_snapshot *s=(void*)(base+FRONT_SNAPSHOT_OFF);
    uint64_t begin=probe_clock();
    do {
        inv_dcache_range(base+FRONT_SNAPSHOT_OFF,64);
        if(s->ready)break;
        if(probe_clock()-begin>12500000u)return -56; /* bounded 500 ms, including abandoned requests */
    } while(1);
    inv_dcache_range(base+FRONT_SNAPSHOT_OFF,sizeof(*s));
    if(s->magic!=FRONT_MAGIC || s->timestamp!=f->timestamp || s->count[0]>FRONT_POINTS || s->count[1]>FRONT_POINTS)return -57;
    f->currid=s->currid;f->num_features=s->num_features;
    for(unsigned cam=0;cam<2;++cam){f->input_count[cam]=s->count[cam];
        memcpy(f->input[cam],(const void*)s->points[cam],s->count[cam]*sizeof(struct front_point));}
    return 0;
}
static void c906_tpu_front(uintptr_t base) {
    struct tpu_probe_control *ctl=(void*)base;struct front_control *f=(void*)(base+FRONT_META_OFF);
    if(base>0x8ff40000u-FRONT_BYTES){ctl->status=-50;ctl->completed=0;goto done;}
    inv_dcache_range(base+FRONT_META_OFF,sizeof(*f));
    inv_dcache_range(base+FRONT_RAW_OFF,2*FRONT_PIXELS);
    inv_dcache_range(base+FRONT_INPUT_MASK_OFF,2*FRONT_PIXELS);
    struct front_ops ops={probe_clock,front_pyramid_tpu,front_flow_tpu,front_equalize_tpu,front_wait_snapshot};
    int result=front_run((void*)base,&ops);ctl->status=result;ctl->completed=result?0:1;
    clean_dcache_range(base+FRONT_META_OFF,sizeof(*f));
    clean_dcache_range(base+FRONT_STATE_OFF,sizeof(struct front_state));
done:
    clean_dcache_range(base,TPU_PROBE_CONTROL_BYTES);
}

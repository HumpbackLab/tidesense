/* Two independent 16-point states. Only TDMA and TIU overlap, never two TIUs. */
#if LK_TPU_DUAL_BATCH
#if !LK_TPU_ROI_CACHE || LK_TPU_BATCH != 16
#error "Dual LK requires ROI cache and 16 lanes"
#endif
struct lk_dual_tensors {
    cvk_tl_t *raw,*weights,*tmpl,*gx,*gy,*sample,*product,*sum[3];
    cvk_tl_t *image,*kx,*ky,*rows,*cols,*current;
};
static void dual_dot(cvk_context_t *c,struct lk_dual_tensors *t,unsigned out,cvk_tl_t *a,cvk_tl_t *b)
{
    mul(c,t->product,a,b);
    cvk_tiu_average_pooling_param_t p={.ifmap=t->product,.ofmap=t->sum[out],
        .kh=LK_TPU_H,.kw=LK_TPU_W,.stride_h=1,.stride_w=1,.avg_pooling_const=0x4370};
    c->ops->tiu_average_pooling(c,&p);
}
static void dual_load(cvk_context_t *c,struct lk_dual_tensors *t,uint64_t phys,unsigned slot,int init,int cached)
{
    load(c,t->weights,phys+LK_TPU_WEIGHTS_OFF+slot*LK_TPU_BATCH*8,0);
    if(init){
        load(c,t->kx,phys+LK_TPU_KERNEL_X_OFF,0);load(c,t->ky,phys+LK_TPU_KERNEL_Y_OFF,0);
        load(c,t->rows,phys+LK_TPU_ROW_MASK_OFF+slot*LK_TPU_BATCH*(LK_TPU_H+1)*2,0);
        load(c,t->cols,phys+LK_TPU_COL_MASK_OFF+slot*LK_TPU_BATCH*(LK_TPU_W+1)*2,0);
        gather(c,t->image,phys+LK_TPU_PLANES_OFF,0x3000+slot*0x100);
    }else if(!cached)gather(c,t->current,phys+LK_TPU_PLANES_OFF,0x4000+slot*0x100);
}
static void dual_compute(cvk_context_t *c,struct lk_dual_tensors *t,int init)
{
    if(init){
        center(c,t->image);
        scharr(c,t->image,t->raw,t->kx,t->rows,t->cols);interpolate(c,t->raw,t->weights,t->gx);
        scharr(c,t->image,t->raw,t->ky,t->rows,t->cols);interpolate(c,t->raw,t->weights,t->gy);
        cvk_tl_t gray=*t->image;gray.start_address+=t->image->stride.h+2;
        interpolate(c,&gray,t->weights,t->tmpl);
        dual_dot(c,t,0,t->gx,t->gx);dual_dot(c,t,1,t->gx,t->gy);dual_dot(c,t,2,t->gy,t->gy);
    }else{
        center_copy(c,t->current,t->raw);interpolate(c,t->raw,t->weights,t->sample);
        cvk_tiu_sub_param_t p={.res_low=t->sample,.a_low=t->tmpl,.b_low=t->sample};c->ops->tiu_sub(c,&p);
        dual_dot(c,t,0,t->gx,t->sample);dual_dot(c,t,1,t->gy,t->sample);
    }
}
static void dual_store(cvk_context_t *c,struct lk_dual_tensors *t,uint64_t phys,unsigned slot,int init)
{
    unsigned count=init?3:2,off=init?LK_TPU_HESS_OFF:LK_TPU_RHS_OFF;
    for(unsigned i=0;i<count;++i)store(c,t->sum[i],phys+off+(slot*count+i)*LK_TPU_BATCH*2);
}
static int lk_tpu_make_dual_jobs(uint8_t *shared,uint64_t phys)
{
    struct lk_tpu_control *m=(void *)(shared+LK_TPU_META_OFF);m->dual_magic=0;
    const char *env=getenv("OPENVINS_TPU_LK_DUAL");if(env && !strcmp(env,"0"))return 0;
    if(m->cached_magic!=LK_TPU_CACHE_MAGIC)return 0;
    if(LK_TPU_BATCH!=16)return -1;
    if(m->dma_bytes[0]>16384 || m->dma_bytes[1]>8192 || m->cached_dma_bytes>4096)return -1;
    uint8_t *buffer=calloc(1,131072);cvk_reg_info_t reg={.chip_ver_str="cv181x",.cmdbuf_size=131072,.cmdbuf=buffer};
    cvk_context_t *c=buffer?cvikernel_register(&reg):NULL;
    cvk_tl_t *allocated[33];unsigned nalloc=0;int ret=-1;
    struct lk_dual_tensors sets[2];memset(sets,0,sizeof(sets));
    if(!c)goto done;
    cvk_tl_shape_t shapes[16]={{1,16,16,17},{1,16,1,4},{1,16,15,16},
        {1,16,15,16},{1,16,15,16},{1,16,15,16},{1,16,15,16},
        {1,16,1,1},{1,16,1,1},{1,16,1,1},{1,16,18,19},
        {1,16,3,3},{1,16,3,3},{1,16,16,1},{1,16,1,17},{1,16,16,17}};
    for(unsigned s=0;s<2;++s){
        cvk_tl_t **fields[]={&sets[s].raw,&sets[s].weights,&sets[s].tmpl,&sets[s].gx,&sets[s].gy,
            &sets[s].sample,&sets[s].product,&sets[s].sum[0],&sets[s].sum[1],&sets[s].sum[2],
            &sets[s].image,&sets[s].kx,&sets[s].ky,&sets[s].rows,&sets[s].cols,&sets[s].current};
        for(unsigned i=0;i<16;++i){*fields[i]=c->ops->lmem_alloc_tensor(c,shapes[i],CVK_FMT_BF16,1);
            if(!*fields[i])goto done;
            allocated[nalloc++]=*fields[i];}
        if(!s){unsigned end=sets[0].current->start_address+sets[0].current->stride.n;
            if(end>=16384 || (end&15))goto done;
            cvk_tl_shape_t pad={1,8,1,(16384-end)/2};
            cvk_tl_t *p=c->ops->lmem_alloc_tensor(c,pad,CVK_FMT_BF16,1);if(!p)goto done;allocated[nalloc++]=p;}
    }
    const char *par=getenv("OPENVINS_TPU_LK_DUAL_PARALLEL");int parallel=!par || strcmp(par,"0");
    m->dual_parallel=parallel;
    for(unsigned job=0;job<LK_TPU_DUAL_JOBS;++job){
        unsigned mask=job==0?3:lk_dual_masks[job-1],cache=job==0?0:lk_dual_cache_masks[job-1];
        int init=job==0;
        if(mask==3 && parallel){
            dual_load(c,&sets[0],phys,0,init,cache&1);
            c->ops->parallel_enable(c);dual_compute(c,&sets[0],init);dual_load(c,&sets[1],phys,1,init,cache&2);c->ops->parallel_disable(c);
            c->ops->parallel_enable(c);dual_compute(c,&sets[1],init);dual_store(c,&sets[0],phys,0,init);c->ops->parallel_disable(c);
            dual_store(c,&sets[1],phys,1,init);
        }else for(unsigned s=0;s<2;++s)if(mask&(1u<<s)){
            dual_load(c,&sets[s],phys,s,init,cache&(1u<<s));dual_compute(c,&sets[s],init);dual_store(c,&sets[s],phys,s,init);
        }
        uint32_t bytes=0;size_t dma=0,pmu=0;uint8_t *commands=c->ops->acquire_cmdbuf(c,&bytes);
        unsigned off=lk_dual_offsets[job];
        if(!commands || cvi181x_dmabuf_size(commands,bytes,&dma,&pmu) || dma>lk_dual_limits[job] ||
           cvi181x_dmabuf_convert(commands,bytes,shared+off) || cvi181x_dmabuf_relocate(shared+off,phys+off,dma,0))goto done;
        m->dual_bytes[job]=dma;
        if(!job)memset(m->dual_init_patch,0xff,sizeof(m->dual_init_patch));
        else memset(m->dual_iter_patch[job-1],0xff,sizeof(m->dual_iter_patch[0]));
        const struct tpu_probe_dma_header *h=(void *)(shared+off);const struct tpu_probe_cpu_desc *desc=(const void *)(h+1);
        unsigned found=0;
        for(unsigned seg=0;seg<h->cpu_count;++seg,++desc){
            uint32_t *td=(void *)(shared+((uint64_t)desc->offset_tdma<<6)-phys);
            for(unsigned i=0;i<(desc->num_tdma&65535);++i,td+=16){unsigned tag=td[15]>>16;
                for(unsigned s=0;s<2;++s){unsigned start=(init?0x3000:0x4000)+s*0x100;
                    if(tag>=start && tag<start+16){unsigned rel=(uint8_t *)td-(shared+off);
                        if(init)m->dual_init_patch[s][tag-start]=rel;
                        else m->dual_iter_patch[job-1][s][tag-start]=rel;
                        ++found;}
                }
            }
        }
        unsigned expected=0;for(unsigned s=0;s<2;++s)if((mask&(1u<<s)) && (init || !(cache&(1u<<s))))expected+=16;
        printf("DUAL job=%u parallel=%d dma=%zu tiu=%u tdma=%u segments=%u patches=%u/%u\n",job,parallel,dma,h->tiu_count,h->tdma_count,h->cpu_count,found,expected);
        if(found!=expected)goto done;
        c->ops->reset(c);
    }
    printf("DUAL SRAM end=%u per_npu=%u\n",sets[1].current->start_address+sets[1].current->stride.n,c->info.lmem_size);
    m->dual_magic=LK_TPU_DUAL_MAGIC;ret=0;
done:
    if(c){while(nalloc)c->ops->lmem_free_tensor(c,allocated[--nalloc]);c->ops->cleanup(c);free(c);}
    free(buffer);return ret;
}
#endif

/* Included after the scalar LK submit helper. Two independent 16-lane states. */
#if LK_TPU_DUAL_BATCH
#if !LK_TPU_ROI_CACHE || LK_TPU_BATCH != 16
#error "Dual LK requires ROI cache and 16 lanes"
#endif
struct lk_dual_state {
    float nx[16],ny[16],hxx[16],hxy[16],hyy[16],pdx[16],pdy[16];
    uint32_t address[16];unsigned char active[16];
    unsigned first,count,any,unchanged;
};
static int lk_dual_roi(uintptr_t base,struct lk_tpu_control *m,unsigned level,unsigned slot,unsigned lane,
                       float x,float y,int init,uint32_t *address)
{
    const struct lk_tpu_level *l=&m->level[level];
    float ux=x-7.5f,uy=y-7.0f;
    int valid=ux>=-16.f && uy>=-15.f && ux<(float)l->width && uy<(float)l->height;
    if(!valid){ux=l->width*0.5f-7.5f;uy=l->height*0.5f-7.0f;}
    int ix=(int)ux,iy=(int)uy;ix-=ux<(float)ix;iy-=uy<(float)iy;
    float fx=ux-ix,fy=uy-iy;
    unsigned short *weights=(void *)(base+LK_TPU_WEIGHTS_OFF+(slot*16+lane)*8);
    weights[0]=lk_tpu_bf16((1-fx)*(1-fy));weights[1]=lk_tpu_bf16(fx*(1-fy));
    weights[2]=lk_tpu_bf16((1-fx)*fy);weights[3]=lk_tpu_bf16(fx*fy);
    uint32_t addr=base+l->planes[init?0:1]+(iy+LK_TPU_BORDER-(init?1:0))*l->row_bytes+ix+LK_TPU_BORDER-(init?1:0);
    if(init){
        uint32_t *td=(void *)(base+lk_dual_offsets[0]+m->dual_init_patch[slot][lane]);td[12]=addr;td[6]=(td[6]&0xffff0000u)|l->row_bytes;
        unsigned short *rows=(void *)(base+LK_TPU_ROW_MASK_OFF+(slot*16+lane)*(LK_TPU_H+1)*2);
        unsigned short *cols=(void *)(base+LK_TPU_COL_MASK_OFF+(slot*16+lane)*(LK_TPU_W+1)*2);
        for(unsigned i=0;i<LK_TPU_H+1;++i)rows[i]=(iy+(int)i>=0 && iy+i<l->height)?0x3f80:0;
        for(unsigned i=0;i<LK_TPU_W+1;++i)cols[i]=(ix+(int)i>=0 && ix+i<l->width)?0x3f80:0;
    }else *address=addr;
    return valid;
}
static int lk_dual_submit(uintptr_t base,struct tpu_probe_control *ctl,struct lk_tpu_control *m,unsigned job)
{
    uint64_t t=probe_clock();
    clean_dcache_range(base+LK_TPU_WEIGHTS_OFF,256);
    if(!job)clean_dcache_range(base+LK_TPU_ROW_MASK_OFF,4096);
    clean_dcache_range(base+lk_dual_offsets[job],m->dual_bytes[job]);m->prepare_ticks+=probe_clock()-t;
    ctl->dmabuf_bytes=m->dual_bytes[job];t=probe_clock();
    int ret=probe_execute_at(ctl,base,lk_dual_offsets[job]);if(ret)probe_resync();
    uint64_t end=probe_clock();m->execute_ticks+=end-t;++m->submissions;
    if(job){++m->dual_packets;if(lk_dual_masks[job-1]!=3)++m->dual_solo_packets;}
    if(m->trace_flags==LK_TPU_TRACE_MAGIC){
        if(m->trace_count<LK_TPU_TRACE_MAX){struct lk_tpu_trace_span *p=(void *)(base+LK_TPU_TRACE_OFF);p[m->trace_count++]=(struct lk_tpu_trace_span){t,end};}
        else m->trace_overflow=1;
    }
    inv_dcache_range(base+LK_TPU_HESS_OFF,512);return ret;
}
static int lk_dual_run(uintptr_t base,struct tpu_probe_control *ctl,struct lk_tpu_control *m)
{
    if(LK_TPU_BATCH!=16 || m->cached_magic!=LK_TPU_CACHE_MAGIC)return -60;
    for(unsigned j=0;j<LK_TPU_DUAL_JOBS;++j){
        if(m->dual_bytes[j]<128 || m->dual_bytes[j]>lk_dual_limits[j])return -60;
        for(unsigned s=0;s<2;++s)for(unsigned k=0;k<16;++k){
            int present=!j || ((lk_dual_masks[j-1]&(1u<<s)) && !(lk_dual_cache_masks[j-1]&(1u<<s)));
            if(!present)continue;
            unsigned off=j?m->dual_iter_patch[j-1][s][k]:m->dual_init_patch[s][k];
            if((off&63u) || off+64>m->dual_bytes[j])return -60;
        }
    }
    uint64_t begin=probe_clock();m->prepare_ticks=m->execute_ticks=m->solve_ticks=0;
    m->submissions=m->valid_count=m->dual_packets=m->dual_solo_packets=0;
    struct lk_dual_state states[2];
    for(unsigned first=0;first<m->count;first+=32){
        for(unsigned s=0;s<2;++s){struct lk_dual_state *st=&states[s];st->first=first+s*16;
            st->count=st->first<m->count?m->count-st->first:0;if(st->count>16)st->count=16;
            for(unsigned k=0;k<st->count;++k){unsigned p=st->first+k;
                st->nx[k]=m->initial[p].x/(1u<<(m->levels-1));st->ny[k]=m->initial[p].y/(1u<<(m->levels-1));
                m->valid[p]=1;m->iterations[p]=0;}
        }
        for(int level=m->levels-1;level>=0;--level){
            uint64_t t=probe_clock();
            for(unsigned s=0;s<2;++s){struct lk_dual_state *st=&states[s];
                for(unsigned k=0;k<16;++k){unsigned p=st->first+k;
                    float x=k<st->count?m->input[p].x/(1u<<level):0,y=k<st->count?m->input[p].y/(1u<<level):0;
                    st->active[k]=lk_dual_roi(base,m,level,s,k,x,y,1,0) && k<st->count;
                    if(k<st->count && level!=(int)m->levels-1){st->nx[k]*=2;st->ny[k]*=2;}
                    const struct lk_tpu_level *l=&m->level[level];
                    st->address[k]=base+l->planes[1]+LK_TPU_BORDER*l->row_bytes+LK_TPU_BORDER;
                }
            }
            m->prepare_ticks+=probe_clock()-t;if(probe_clock()-begin>12500000u)return -21;
            int ret=lk_dual_submit(base,ctl,m,0);if(ret)return ret;
            unsigned short *h=(void *)(base+LK_TPU_HESS_OFF);t=probe_clock();
            for(unsigned s=0;s<2;++s){struct lk_dual_state *st=&states[s];
                for(unsigned k=0;k<st->count;++k){
                    st->hxx[k]=lk_tpu_float(h[s*48+k]);st->hxy[k]=lk_tpu_float(h[s*48+16+k]);st->hyy[k]=lk_tpu_float(h[s*48+32+k]);
                    st->pdx[k]=st->pdy[k]=0;float a=st->hxx[k],b=st->hxy[k],c=st->hyy[k];
                    float eig=(a+c-sqrtf((a-c)*(a-c)+4*b*b))/(2*LK_TPU_PIXELS);
                    if(a*c-b*b<1e-7f || eig<0.1024f)st->active[k]=0;
                    if(!st->active[k] && !level)m->valid[st->first+k]=0;
                }
            }
            m->solve_ticks+=probe_clock()-t;
            for(unsigned it=0;it<m->max_iterations;++it){
                t=probe_clock();unsigned mask=0,cache=0;
                for(unsigned s=0;s<2;++s){struct lk_dual_state *st=&states[s];st->any=st->unchanged=0;
                    for(unsigned k=0;k<st->count;++k)if(st->active[k]){
                        uint32_t old=st->address[k];int valid=lk_dual_roi(base,m,level,s,k,st->nx[k],st->ny[k],0,&st->address[k]);
                        if(!valid){st->active[k]=0;if(!level)m->valid[st->first+k]=0;}
                        st->any+=st->active[k];if(st->active[k] && it && old==st->address[k])++st->unchanged;
                    }
                    if(st->any){mask|=1u<<s;
                        if(it && st->unchanged==st->any){cache|=1u<<s;++m->cache_hits;}else ++m->cache_misses;
#ifdef LK_TPU_PROFILE_OCCUPANCY
                        ++m->iteration_jobs;m->lane_slots+=16;m->active_lane_slots+=st->any;m->reusable_active_slots+=st->unchanged;
                        if(st->unchanged==st->any)++m->reusable_jobs;
#endif
                    }
                }
                if(!mask){m->prepare_ticks+=probe_clock()-t;break;}
                unsigned job=mask==3?1+cache:mask==1?5+(cache&1):7+((cache>>1)&1);
                for(unsigned s=0;s<2;++s)if((mask&(1u<<s)) && !(cache&(1u<<s))){
                    for(unsigned k=0;k<16;++k){uint32_t *td=(void *)(base+lk_dual_offsets[job]+m->dual_iter_patch[job-1][s][k]);
                        td[12]=states[s].address[k];td[6]=(td[6]&0xffff0000u)|m->level[level].row_bytes;}
                }
                m->prepare_ticks+=probe_clock()-t;if(probe_clock()-begin>12500000u)return -21;
                ret=lk_dual_submit(base,ctl,m,job);if(ret)return ret;
                unsigned short *rhs=(void *)(base+LK_TPU_RHS_OFF);t=probe_clock();
                for(unsigned s=0;s<2;++s){struct lk_dual_state *st=&states[s];
                    for(unsigned k=0;k<st->count;++k)if(st->active[k]){
                        float bx=lk_tpu_float(rhs[s*32+k]),by=lk_tpu_float(rhs[s*32+16+k]);
                        float det=st->hxx[k]*st->hyy[k]-st->hxy[k]*st->hxy[k];
                        float dx=(st->hyy[k]*bx-st->hxy[k]*by)/det,dy=(st->hxx[k]*by-st->hxy[k]*bx)/det;
                        if(!isfinite(dx) || !isfinite(dy)){st->active[k]=0;if(!level)m->valid[st->first+k]=0;continue;}
                        st->nx[k]+=dx;st->ny[k]+=dy;++m->iterations[st->first+k];
                        if(dx*dx+dy*dy<=0.0001f)st->active[k]=0;
                        else if(it && fabsf(dx+st->pdx[k])<0.01f && fabsf(dy+st->pdy[k])<0.01f){st->nx[k]-=dx*0.5f;st->ny[k]-=dy*0.5f;st->active[k]=0;}
                        st->pdx[k]=dx;st->pdy[k]=dy;
                    }
                }
                m->solve_ticks+=probe_clock()-t;
            }
        }
        for(unsigned s=0;s<2;++s)for(unsigned k=0;k<states[s].count;++k){unsigned p=states[s].first+k;
            m->output[p]=(struct lk_tpu_point){states[s].nx[k],states[s].ny[k]};m->valid_count+=m->valid[p]!=0;}
    }
    m->total_ticks=probe_clock()-begin;return 0;
}
#endif

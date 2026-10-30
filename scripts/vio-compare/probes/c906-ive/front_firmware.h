/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional IVE callbacks inside the single C906 frontend request.
 */
static struct ive_control *front_ive;
#ifdef FRONT_IVE_HIST
static int front_ive_histogram(void *memory,unsigned cam,unsigned *bins){
 (void)memory;if(!front_ive || cam>1)return -87;
 int ret=ive_execute_program(front_ive,cam);if(ret)return ret;
 uintptr_t address=(uintptr_t)front_ive->arena+IVE_HIST_OFF+cam*1024;
 inv_dcache_range(address,1024);const unsigned *counts=(void*)address;
 unsigned total=0;for(unsigned i=0;i<256;++i){bins[i]=counts[i];total+=counts[i];}
 return total==front_ive->width*front_ive->height?0:-88;
}
#endif
#ifdef FRONT_IVE_ST
static int front_ive_corners(void *memory,const struct lk_tpu_level *level,const unsigned char **response){
 if(!front_ive || level->planes[0]<FRONT_PYR_OFF || level->width!=front_ive->width || level->height!=front_ive->height || level->row_bytes!=front_image_stride(level->width))return -87;
 unsigned bank=(level->planes[0]-FRONT_PYR_OFF)/FRONT_CAM_BYTES;if(bank>3)return -87;
 /* LUT writes the image, C906 writes its reflected halo. Publish the shared
  * edge cache lines before the independent IVE engine reads this plane. */
 clean_dcache_range((uintptr_t)memory+level->planes[0],(level->height+2*LK_TPU_BORDER)*level->row_bytes);
 int ret=ive_execute_program(front_ive,2+bank);if(ret)return ret;
 uintptr_t address=(uintptr_t)front_ive->arena+IVE_ST_OUT_OFF;
 inv_dcache_range(address,level->width*level->height);*response=(const void*)address;return 0;
}
#endif
static int front_ive_begin(uintptr_t base,struct front_control *f,struct front_ops *ops){
 front_ive=0;struct ive_control *c=(void*)(base+IVE_META_OFF);
 inv_dcache_range((uintptr_t)c,sizeof(*c));
 if(c->magic!=IVE_MAGIC || !c->flags)return 0;
 front_ive=ive_bind(base);if(!front_ive)return -81;
 if(!FRONT_ALIGNED_IMAGES || c->width!=f->width || c->height!=f->height || c->pack_input)return -87;
 c->histogram_ticks=c->st_ticks=c->pack_ticks=0;c->histogram_calls=c->st_calls=0;
#ifdef FRONT_IVE_HIST
 if(c->flags&IVE_FLAG_HIST)ops->histogram=front_ive_histogram;
#endif
#ifdef FRONT_IVE_ST
 if(c->flags&IVE_FLAG_ST)ops->corners=front_ive_corners;
#endif
 return 0;
}
static void front_ive_end(int result){
 if(!front_ive)return;front_ive->status=result;
 clean_dcache_range((uintptr_t)front_ive,sizeof(*front_ive));front_ive=0;
}

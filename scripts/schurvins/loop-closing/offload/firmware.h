/* Included by the opt-in C906 TPU probe firmware. */
#include "shared.h"
static void c906_loop_match(uintptr_t base) {
    struct tpu_probe_control *ctl=(void*)base;
    struct loop_match_meta *m=(void*)(base+LOOP_MATCH_META_OFF);
    const uint64_t begin=probe_clock();
    ctl->status=-20;ctl->completed=0;
    m->tpu_calls=m->reductions=0;m->tpu_ticks=m->reduction_ticks=0;
    if(m->magic!=LOOP_MATCH_MAGIC || m->version!=1 || !m->pairs || m->pairs>LOOP_MATCH_BATCH ||
       !m->queries || m->queries>LOOP_MATCH_QUERIES || m->use_tpu>1 ||
       !ctl->timeout_us || ctl->timeout_us>10000u || ctl->dmabuf_bytes<128 ||
       ctl->dmabuf_bytes>LOOP_MATCH_META_OFF-TPU_PROBE_DMA_OFF) goto done;
    if(m->use_tpu) {
        uint64_t start=probe_clock();
        ctl->status=probe_execute_at(ctl,base,TPU_PROBE_DMA_OFF);
        m->tpu_ticks=probe_clock()-start;m->tpu_calls=1;
        if(ctl->status) goto done;
        inv_dcache_range(base+LOOP_MATCH_DIST_OFF,LOOP_MATCH_BATCH*2);
    } else {
        const uint8_t *a=(void*)(base+LOOP_MATCH_A_OFF),*b=(void*)(base+LOOP_MATCH_B_OFF);
        const uint8_t *popcount=(void*)(base+LOOP_MATCH_LUT_OFF);
        uint16_t *out=(void*)(base+LOOP_MATCH_DIST_OFF);
        for(unsigned i=0;i<m->pairs;++i) {
            unsigned d=0;for(unsigned j=0;j<32;++j)d+=popcount[a[i*32+j]^b[i*32+j]];
            union {uint32_t u;float f;} value;value.f=(float)d;out[i]=value.u>>16;
        }
        clean_dcache_range(base+LOOP_MATCH_DIST_OFF,LOOP_MATCH_BATCH*2);
    }
    { uint64_t start=probe_clock();
      ctl->status=loop_match_reduce((void*)base);m->reduction_ticks=probe_clock()-start; }
    if(!ctl->status) ctl->completed=1;
done:
    if(m->use_tpu && ctl->status)probe_resync();
    ctl->total_ticks=probe_clock()-begin;
    clean_dcache_range(base+LOOP_MATCH_META_OFF,64);
    clean_dcache_range(base+LOOP_MATCH_RESULT_OFF,LOOP_MATCH_QUERIES*sizeof(struct loop_match_result));
    clean_dcache_range(base,TPU_PROBE_CONTROL_BYTES);
}

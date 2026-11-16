/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "shared.h"
#include "../../../vio-compare/probes/c906-tpu/shared.h"
#include <cvikernel/cvikernel.h>
#include <stdlib.h>
#include <stdio.h>
extern int cvi181x_dmabuf_size(uint8_t *,size_t,size_t *,size_t *);
extern int cvi181x_dmabuf_convert(uint8_t *,size_t,uint8_t *);
extern int cvi181x_dmabuf_relocate(uint8_t *,uint64_t,uint32_t,uint32_t);
static void load(cvk_context_t *c,cvk_tl_t *dst,uint64_t addr) {
    cvk_tg_t g={.start_address=addr,.fmt=dst->fmt,.shape={dst->shape.n,dst->shape.c,dst->shape.h,dst->shape.w}};
    g.stride=c->ops->tg_default_stride(c,g.shape,g.fmt);
    cvk_tdma_g2l_tensor_copy_param_t p={.src=&g,.dst=dst};c->ops->tdma_g2l_tensor_copy(c,&p);
}
/* Pure command generation. Execution is by C906, or Linux in the comparison probe. */
int loop_match_make_job(uint8_t *shared,uint64_t phys,uint8_t **raw_out,uint32_t *raw_bytes) {
    uint8_t *storage=calloc(1,65536);int result=-1;
    cvk_reg_info_t reg={.chip_ver_str="cv181x",.cmdbuf_size=65536,.cmdbuf=storage};
    cvk_context_t *c=storage?cvikernel_register(&reg):NULL;
    cvk_tl_t *allocated[5];unsigned count=0;
    if(!c) goto done;
    const cvk_tl_shape_t shape={1,8,LOOP_MATCH_BATCH/8,32};
    const cvk_tl_shape_t lutshape={1,8,16,16},sumshape={1,8,LOOP_MATCH_BATCH/8,1};
    cvk_tl_t *a=c->ops->lmem_alloc_tensor(c,shape,CVK_FMT_U8,1);if(!a)goto done;allocated[count++]=a;
    cvk_tl_t *b=c->ops->lmem_alloc_tensor(c,shape,CVK_FMT_U8,1);if(!b)goto done;allocated[count++]=b;
    cvk_tl_t *f=c->ops->lmem_alloc_tensor(c,shape,CVK_FMT_BF16,1);if(!f)goto done;allocated[count++]=f;
    cvk_tl_t *table=c->ops->lmem_alloc_tensor(c,lutshape,CVK_FMT_U8,1);if(!table)goto done;allocated[count++]=table;
    cvk_tl_t *sum=c->ops->lmem_alloc_tensor(c,sumshape,CVK_FMT_BF16,1);if(!sum)goto done;allocated[count++]=sum;
    load(c,a,phys+LOOP_MATCH_A_OFF);load(c,b,phys+LOOP_MATCH_B_OFF);load(c,table,phys+LOOP_MATCH_LUT_OFF);
    cvk_tiu_xor_int8_param_t x={.res=a,.a=a,.b=b};c->ops->tiu_xor_int8(c,&x);
    cvk_tiu_lookup_table_param_t lut={.ofmap=b,.ifmap=a,.table=table};c->ops->tiu_lookup_table(c,&lut);
    cvk_tdma_l2l_tensor_copy_param_t convert={.src=b,.dst=f};c->ops->tdma_l2l_bf16_tensor_copy(c,&convert);
    cvk_tiu_average_pooling_param_t pool={.ofmap=sum,.ifmap=f,.kh=1,.kw=32,
        .stride_h=1,.stride_w=1,.avg_pooling_const=0x4200}; /* 32 / area = 1 */
    c->ops->tiu_average_pooling(c,&pool);
    cvk_tg_t g={.start_address=phys+LOOP_MATCH_DIST_OFF,.fmt=CVK_FMT_BF16,.shape={1,8,LOOP_MATCH_BATCH/8,1}};
    g.stride=c->ops->tg_default_stride(c,g.shape,g.fmt);
    cvk_tdma_l2g_tensor_copy_param_t store={.src=sum,.dst=&g};c->ops->tdma_l2g_bf16_tensor_copy(c,&store);
    uint32_t bytes=0;size_t dma=0,pmu=0;
    uint8_t *commands=c->ops->acquire_cmdbuf(c,&bytes);
    if(!commands || !bytes || cvi181x_dmabuf_size(commands,bytes,&dma,&pmu) ||
       dma>LOOP_MATCH_META_OFF-TPU_PROBE_DMA_OFF ||
       cvi181x_dmabuf_convert(commands,bytes,shared+TPU_PROBE_DMA_OFF) ||
       cvi181x_dmabuf_relocate(shared+TPU_PROBE_DMA_OFF,phys+TPU_PROBE_DMA_OFF,dma,0))goto done;
    ((struct tpu_probe_control*)shared)->dmabuf_bytes=dma;
    fprintf(stderr,"loop Hamming kernel: commands=%u dma=%zu potential_pmu=%zu\n",bytes,dma,pmu);
    if(raw_out) {
        *raw_out=malloc(bytes);if(!*raw_out)goto done;
        __builtin_memcpy(*raw_out,commands,bytes);*raw_bytes=bytes;
    }
    result=0;
done:
    if(c) {while(count)c->ops->lmem_free_tensor(c,allocated[--count]);c->ops->cleanup(c);free(c);}
    free(storage);return result;
}

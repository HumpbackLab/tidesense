/* SPDX-License-Identifier: GPL-3.0-or-later
 * Generate persistent UINT8 Gaussian 5x5 / stride-2 pyramid jobs on the host.
 * C906 relocates only per-frame source/destination planes and submits all jobs.
 */
#include "front_shared.h"
#include "cvikernel/cvikernel.h"
#include <stdlib.h>
#include <string.h>
extern int cvi181x_dmabuf_size(unsigned char*,size_t,size_t*,size_t*);
extern int cvi181x_dmabuf_convert(unsigned char*,size_t,unsigned char*);
extern int cvi181x_dmabuf_relocate(unsigned char*,probe_u64,unsigned,unsigned);
static void copy(cvk_context_t *c,cvk_tl_t *local,probe_u64 address,unsigned stride,unsigned tag,int output) {
    cvk_tg_t g={.fmt=local->fmt,.start_address=address,.shape={1,1,local->shape.h,local->shape.w}};
    g.stride=c->ops->tg_default_stride(c,g.shape,g.fmt);g.stride.h=stride;
    c->ops->set_layer_id(c,tag);
    if(output){cvk_tdma_l2g_tensor_copy_param_t p={.src=local,.dst=&g,.layer_id=tag};c->ops->tdma_l2g_tensor_copy(c,&p);}
    else{cvk_tdma_g2l_tensor_copy_param_t p={.src=&g,.dst=local,.layer_id=tag};c->ops->tdma_g2l_tensor_copy(c,&p);}
    c->ops->set_layer_id(c,0);
}
int front_make_jobs(unsigned char *base,probe_u64 phys,unsigned width,unsigned height) {
    struct front_control *f=(void*)(base+FRONT_META_OFF);
    unsigned char *storage=calloc(1,131072);cvk_reg_info_t reg={.chip_ver_str="cv181x",.cmdbuf_size=131072,.cmdbuf=storage};
    cvk_context_t *c=storage?cvikernel_register(&reg):NULL;int result=-1;
    if(!c || c->info.npu_num!=8)goto done;
    unsigned offsets[5],widths[5],heights[5],strides[5],offset=FRONT_PYR_OFF;
    for(unsigned k=0;k<5;++k){offsets[k]=offset;widths[k]=width;heights[k]=height;strides[k]=width+2*LK_TPU_BORDER;
        offset+=(height+2*LK_TPU_BORDER)*strides[k];offset=(offset+63)&~63u;width=(width+1)/2;height=(height+1)/2;}
    const signed char kernel[5]={1,4,6,4,1};
    for(unsigned ch=0;ch<8;++ch)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)
        base[FRONT_WEIGHTS_OFF+ch*25+y*5+x]=kernel[x]*kernel[y];
    for(unsigned level=1;level<5;++level){
        cvk_tl_shape_t kernelshape={1,8,5,5};
        cvk_tl_t *weight=c->ops->lmem_alloc_tensor(c,kernelshape,CVK_FMT_I8,1);
        if(!weight)goto done;
        cvk_tg_t kg={.fmt=CVK_FMT_I8,.start_address=phys+FRONT_WEIGHTS_OFF,.shape={1,8,5,5}};
        kg.stride=c->ops->tg_default_stride(c,kg.shape,kg.fmt);
        cvk_tdma_g2l_tensor_copy_param_t kl={.src=&kg,.dst=weight};c->ops->tdma_g2l_tensor_copy(c,&kl);
        unsigned strip=widths[level]/8;if(!strip || strip*8!=widths[level])goto done;
        for(unsigned row=0;row<heights[level];row+=64){unsigned h=heights[level]-row;if(h>64)h=64;
            cvk_tl_shape_t ishape={1,8,2*h+3,2*strip+3},oshape={1,8,h,strip};
            cvk_tl_t *in=c->ops->lmem_alloc_tensor(c,ishape,CVK_FMT_U8,1),*out=c->ops->lmem_alloc_tensor(c,oshape,CVK_FMT_U8,1);
            if(!in || !out)goto done;
            for(unsigned ch=0;ch<8;++ch){cvk_tl_t one=*in;one.shape.c=1;one.start_address+=ch*c->info.lmem_size;
                probe_u64 address=phys+offsets[level-1]+(LK_TPU_BORDER+2*row-2)*strides[level-1]+LK_TPU_BORDER+2*ch*strip-2;
                copy(c,&one,address,strides[level-1],0x3000+ch,0);}
            cvk_tiu_depthwise_pt_convolution_param_t conv={.ifmap=in,.ofmap=out,.weight=weight,
                .stride_h=2,.stride_w=2,.dilation_h=1,.dilation_w=1,.rshift_bits=8};
            c->ops->tiu_pt_depthwise_convolution(c,&conv);
            for(unsigned ch=0;ch<8;++ch){cvk_tl_t one=*out;one.shape.c=1;one.start_address+=ch*c->info.lmem_size;
                probe_u64 address=phys+offsets[level]+(LK_TPU_BORDER+row)*strides[level]+LK_TPU_BORDER+ch*strip;
                copy(c,&one,address,strides[level],0x4000+ch,1);}
            c->ops->lmem_free_tensor(c,out);c->ops->lmem_free_tensor(c,in);
        }
        c->ops->lmem_free_tensor(c,weight);
        unsigned command_bytes=0;size_t dma_bytes=0,pmu=0;unsigned char *commands=c->ops->acquire_cmdbuf(c,&command_bytes);
        unsigned joboff=FRONT_PYR_DMA_OFF+(level-1)*FRONT_PYR_JOB_BYTES;
        if(!commands || cvi181x_dmabuf_size(commands,command_bytes,&dma_bytes,&pmu) || dma_bytes>FRONT_PYR_JOB_BYTES ||
           cvi181x_dmabuf_convert(commands,command_bytes,base+joboff) || cvi181x_dmabuf_relocate(base+joboff,phys+joboff,dma_bytes,0))goto done;
        f->pyramid_dma_bytes[level-1]=dma_bytes;f->pyramid_patch_count[level-1]=0;
        const struct tpu_probe_dma_header *header=(void*)(base+joboff);
        const struct tpu_probe_cpu_desc *d=(const void*)(header+1);
        for(unsigned segment=0;segment<header->cpu_count;++segment,++d){
            unsigned *td=(void*)(base+((probe_u64)d->offset_tdma<<6)-phys);
            for(unsigned k=0;k<(d->num_tdma&65535);++k,td+=16){unsigned tag=td[15]>>16,word;
                if(tag>=0x3000 && tag<0x3008)word=12;
                else if(tag>=0x4000 && tag<0x4008)word=11;
                else continue;
                unsigned n=f->pyramid_patch_count[level-1]++;if(n==64)goto done;
                f->pyramid_patch[level-1][n]=(struct front_patch){(unsigned)((unsigned char*)td-(base+joboff)),word,td[word]-(unsigned)phys-FRONT_PYR_OFF};
            }
        }
        c->ops->reset(c);
    }
    // Apply the C906-generated UINT8 equalization table to a complete image.
    cvk_tl_shape_t table_shape={1,8,16,16};
    cvk_tl_t *table=c->ops->lmem_alloc_tensor(c,table_shape,CVK_FMT_U8,1);
    if(!table)goto done;
    cvk_tg_t tg={.fmt=CVK_FMT_U8,.start_address=phys+FRONT_LUT_OFF,.shape={1,8,16,16}};
    tg.stride=c->ops->tg_default_stride(c,tg.shape,tg.fmt);
    cvk_tdma_g2l_tensor_copy_param_t table_load={.src=&tg,.dst=table};c->ops->tdma_g2l_tensor_copy(c,&table_load);
    unsigned image_width=widths[0],image_height=heights[0],strip=image_width/8;
    for(unsigned row=0;row<image_height;row+=64){unsigned h=image_height-row;if(h>64)h=64;
        cvk_tl_shape_t shape={1,8,h,strip};
        cvk_tl_t *in=c->ops->lmem_alloc_tensor(c,shape,CVK_FMT_U8,1),*out=c->ops->lmem_alloc_tensor(c,shape,CVK_FMT_U8,1);
        if(!in || !out)goto done;
        for(unsigned ch=0;ch<8;++ch){cvk_tl_t one=*in;one.shape.c=1;one.start_address+=ch*c->info.lmem_size;
            copy(c,&one,phys+FRONT_RAW_OFF+row*image_width+ch*strip,image_width,0x5000+ch,0);}
        cvk_tiu_lookup_table_param_t lut={.ofmap=out,.ifmap=in,.table=table};c->ops->tiu_lookup_table(c,&lut);
        for(unsigned ch=0;ch<8;++ch){cvk_tl_t one=*out;one.shape.c=1;one.start_address+=ch*c->info.lmem_size;
            copy(c,&one,phys+FRONT_PYR_OFF+(LK_TPU_BORDER+row)*strides[0]+LK_TPU_BORDER+ch*strip,strides[0],0x6000+ch,1);}
        c->ops->lmem_free_tensor(c,out);c->ops->lmem_free_tensor(c,in);
    }
    c->ops->lmem_free_tensor(c,table);
    unsigned eq_command_bytes=0;size_t eq_dma_bytes=0,eq_pmu=0;
    unsigned char *eq_commands=c->ops->acquire_cmdbuf(c,&eq_command_bytes);
    if(!eq_commands || cvi181x_dmabuf_size(eq_commands,eq_command_bytes,&eq_dma_bytes,&eq_pmu) || eq_dma_bytes>131072 ||
        cvi181x_dmabuf_convert(eq_commands,eq_command_bytes,base+FRONT_EQ_DMA_OFF) ||
        cvi181x_dmabuf_relocate(base+FRONT_EQ_DMA_OFF,phys+FRONT_EQ_DMA_OFF,eq_dma_bytes,0))goto done;
    f->equalize_dma_bytes=eq_dma_bytes;f->equalize_patch_count=0;
    const struct tpu_probe_dma_header *eh=(void*)(base+FRONT_EQ_DMA_OFF);
    const struct tpu_probe_cpu_desc *ed=(const void*)(eh+1);
    for(unsigned segment=0;segment<eh->cpu_count;++segment,++ed){
        unsigned *td=(void*)(base+((probe_u64)ed->offset_tdma<<6)-phys);
        for(unsigned k=0;k<(ed->num_tdma&65535);++k,td+=16){unsigned tag=td[15]>>16,word,origin;
            if(tag>=0x5000 && tag<0x5008){word=12;origin=FRONT_RAW_OFF;}
            else if(tag>=0x6000 && tag<0x6008){word=11;origin=FRONT_PYR_OFF;}
            else continue;
            unsigned n=f->equalize_patch_count++;if(n==128)goto done;
            f->equalize_patch[n]=(struct front_patch){(unsigned)((unsigned char*)td-(base+FRONT_EQ_DMA_OFF)),word,td[word]-(unsigned)phys-origin};
        }
    }
    result=0;
done:
    if(c){c->ops->cleanup(c);free(c);}free(storage);return result;
}

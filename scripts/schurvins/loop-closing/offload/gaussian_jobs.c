/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gaussian_jobs.h"
#include <cvikernel/cvikernel.h>
#include <stdlib.h>
#include <string.h>

static void transfer(cvk_context_t* c,cvk_tl_t* tensor,uint64_t address,
                     unsigned stride,int output) {
  cvk_tg_t g={.start_address=address,.fmt=tensor->fmt,
    .shape={tensor->shape.n,tensor->shape.c,tensor->shape.h,tensor->shape.w}};
  g.stride=c->ops->tg_default_stride(c,g.shape,g.fmt);
  if(stride)g.stride.h=stride;
  if(output) {
    cvk_tdma_l2g_tensor_copy_param_t p={.src=tensor,.dst=&g};
    c->ops->tdma_l2g_tensor_copy(c,&p);
  } else {
    cvk_tdma_g2l_tensor_copy_param_t p={.src=&g,.dst=tensor};
    c->ops->tdma_g2l_tensor_copy(c,&p);
  }
}
int schur_gaussian_make_job(uint64_t physical,const struct gaussian_level* levels,
                           unsigned count,uint32_t weights_offset,
                           uint8_t** commands,uint32_t* command_bytes) {
  const unsigned capacity=1024*1024;
  uint8_t* storage=calloc(1,capacity);int result=-1;
  cvk_reg_info_t registration={.chip_ver_str="cv181x",.cmdbuf_size=capacity,.cmdbuf=storage};
  cvk_context_t* c=storage?cvikernel_register(&registration):NULL;
  cvk_tl_t* allocated[5];unsigned used=0;
  if(!c || c->info.npu_num!=8 || !count || count>8)goto done;
  const cvk_tl_shape_t horizontal_shape={1,8,1,7},vertical_shape={1,8,7,1};
  cvk_tl_t* horizontal=c->ops->lmem_alloc_tensor(c,horizontal_shape,CVK_FMT_I8,1);
  if(!horizontal)goto done;allocated[used++]=horizontal;
  cvk_tl_t* vertical=c->ops->lmem_alloc_tensor(c,vertical_shape,CVK_FMT_I8,1);
  if(!vertical)goto done;allocated[used++]=vertical;
  transfer(c,horizontal,physical+weights_offset,0,0);
  transfer(c,vertical,physical+weights_offset+56,0,0);
  for(unsigned level=0;level<count;++level) {
    const struct gaussian_level* l=levels+level;
    if(!l->width || l->width>640 || !l->height || l->height>480)goto done;
    const unsigned strip=(l->width+7)/8;
    for(unsigned row=0;row<l->height;row+=64) {
      const unsigned height=l->height-row<64?l->height-row:64;
      cvk_tl_shape_t ishape={1,8,height+6,strip+6};
      cvk_tl_shape_t mshape={1,8,height+6,strip},oshape={1,8,height,strip};
      cvk_tl_t* input=c->ops->lmem_alloc_tensor(c,ishape,CVK_FMT_U8,1);
      if(!input)goto done;allocated[used++]=input;
      cvk_tl_t* middle=c->ops->lmem_alloc_tensor(c,mshape,CVK_FMT_U8,1);
      if(!middle)goto done;allocated[used++]=middle;
      cvk_tl_t* output=c->ops->lmem_alloc_tensor(c,oshape,CVK_FMT_U8,1);
      if(!output)goto done;allocated[used++]=output;
      for(unsigned channel=0;channel<8;++channel) {
        cvk_tl_t one=*input;one.shape.c=1;one.start_address+=channel*c->info.lmem_size;
        transfer(c,&one,physical+l->input_offset+row*l->input_stride+channel*strip,l->input_stride,0);
      }
      cvk_tiu_depthwise_pt_convolution_param_t h={.ifmap=input,.ofmap=middle,.weight=horizontal,
        .stride_h=1,.stride_w=1,.dilation_h=1,.dilation_w=1,.rshift_bits=9};
      cvk_tiu_depthwise_pt_convolution_param_t v={.ifmap=middle,.ofmap=output,.weight=vertical,
        .stride_h=1,.stride_w=1,.dilation_h=1,.dilation_w=1,.rshift_bits=9};
      c->ops->tiu_pt_depthwise_convolution(c,&h);
      c->ops->tiu_pt_depthwise_convolution(c,&v);
      for(unsigned channel=0;channel<8;++channel) {
        cvk_tl_t one=*output;one.shape.c=1;one.start_address+=channel*c->info.lmem_size;
        transfer(c,&one,physical+l->output_offset+row*l->output_stride+channel*strip,l->output_stride,1);
      }
      for(unsigned i=0;i<3;++i)c->ops->lmem_free_tensor(c,allocated[--used]);
    }
  }
  uint32_t bytes=0;uint8_t* raw=c->ops->acquire_cmdbuf(c,&bytes);
  if(!raw || !bytes)goto done;
  *commands=malloc(bytes);if(!*commands)goto done;
  memcpy(*commands,raw,bytes);*command_bytes=bytes;result=0;
done:
  if(c){while(used)c->ops->lmem_free_tensor(c,allocated[--used]);c->ops->cleanup(c);free(c);}
  free(storage);return result;
}

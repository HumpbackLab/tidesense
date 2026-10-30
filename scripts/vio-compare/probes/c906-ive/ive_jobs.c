/* SPDX-License-Identifier: GPL-2.0-or-later
 * Host-only capture of SDK register setup. No runtime IVE calls on A53.
 */
#include "shared.h"
#include "linux/cvi_comm_ive.h"
#include "cvi_ive_reg.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
static pthread_mutex_t generator_lock=PTHREAD_MUTEX_INITIALIZER;
static struct ive_program *capture;
static unsigned shadow[0x4000/4],repeat_guard;
static int failed;
static void emit(unsigned off,unsigned value){
 if(capture->count>=sizeof(capture->commands)/sizeof(capture->commands[0])){failed=1;return;}
 capture->commands[capture->count++]=(struct ive_reg_command){off,value};
}
static void write_reg(unsigned value,uintptr_t addr){
 if(addr<0x10000 || addr>=0x14000 || (addr&3)){failed=1;return;}
 addr-=0x10000;shadow[addr/4]=value;emit(addr==0x138?IVE_CMD_NORM:addr,value);
}
static unsigned read_reg(uintptr_t addr){
 if(addr<0x10000 || addr>=0x14000 || (addr&3)){failed=1;return 0;}
 return shadow[(addr-0x10000)/4];
}
static void ive_emit_go(IVE_TOP_C *top,int mask){
 if(!mask)mask=IVE_TOP_REG_FRAME_DONE_FILTEROP_WDMA_Y_MASK;
 write_reg(0,0x10094);emit(IVE_CMD_CLEAR,0x90);emit(IVE_CMD_CLEAR,0x98);
 top->REG_1.reg_fmt_vld_fg=1;write_reg(top->REG_1.val,0x10004);emit(IVE_CMD_WAIT,mask);
}
static bool ive_emit_repeat_begin(void){repeat_guard=capture->count;emit(IVE_CMD_IF_SAT,0);return true;}
static void ive_emit_repeat_end(void){if(repeat_guard<capture->count)capture->commands[repeat_guard].value=capture->count-repeat_guard-1;}
#define writel(v,a) write_reg((v),(uintptr_t)(a))
#define readl(a) read_reg((uintptr_t)(a))
#define udelay(n) emit(IVE_CMD_DELAY,(n))
#define kzalloc(n,f) calloc(1,(n))
#define kfree(p) free(p)
#define GFP_ATOMIC 0
#define CVI_DBG_INFO(...) ((void)0)
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define reinit_completion(...) ((void)0)
#define dump_ive_image(...) ((void)0)
#define dump_ive_mem(...) ((void)0)
#define copy_to_user(...) 0
#include "ive_sdk_generated.inc"
int ive_make_program(struct ive_program *out,unsigned kind,unsigned width,unsigned height,unsigned stride,
                     probe_u32 source,probe_u32 destination,probe_u32 scratch,unsigned quality){
 if(!out || kind>1 || width<32 || width>640 || height<32 || height>480 || stride<width || stride>2048 || !quality || quality>255)return -1;
 pthread_mutex_lock(&generator_lock);
 capture=out;out->count=0;failed=0;memset(shadow,0,sizeof(shadow));assign_ive_block_addr((void*)0x10000);
 struct cvi_ive_device device={0};IVE_IMAGE_S input={0};input.enType=IVE_IMAGE_TYPE_U8C1;
 input.u32Width=width;input.u32Height=height;input.u32Stride[0]=stride;input.u64PhyAddr[0]=source;
 int status;
 if(kind==0){IVE_MEM_INFO_S dst={0};dst.u64PhyAddr=destination;dst.u32Size=1024;status=cvi_ive_Hist(&device,&input,&dst,1);}
 else{IVE_IMAGE_S dst=input;dst.u32Stride[0]=width;dst.u64PhyAddr[0]=destination;
  IVE_ST_CANDI_CORNER_CTRL_S ctrl={0};ctrl.stMem.u64PhyAddr=scratch;ctrl.stMem.u32Size=IVE_SCRATCH_BYTES;ctrl.u0q8QualityLevel=quality;
  status=cvi_ive_STCandiCorner(&device,&input,&dst,&ctrl,1);
 }
 if(failed)status=-3;pthread_mutex_unlock(&generator_lock);return status;
}

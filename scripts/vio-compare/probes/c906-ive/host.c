/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include "cvi_sys.h"
#include "linux/cvi_ive_ioctl.h"
#include "shared.h"
#include "../c906-frontend/front_shared.h"
_Static_assert(IVE_META_OFF+sizeof(struct ive_control)<=FRONT_PYR_OFF,"IVE metadata overlaps pyramid");
_Static_assert(sizeof(struct ive_program)==IVE_PROGRAM_BYTES,"IVE register program size");
_Static_assert(IVE_PROGRAM_OFF+IVE_PROGRAMS*IVE_PROGRAM_BYTES<=IVE_HIST_OFF,"IVE program arena");
_Static_assert(IVE_SCRATCH_OFF+IVE_SCRATCH_BYTES<=IVE_ST_OUT_OFF,"IVE workspace arena");
_Static_assert(IVE_ST_OUT_OFF+FRONT_PIXELS<=IVE_PACK_OFF,"IVE output overlaps packed input");
_Static_assert(IVE_PACK_OFF+FRONT_PIXELS<=IVE_ARENA_BYTES,"IVE packed input arena");
#define IVE_IOCTL _IOW('T',5,uint32_t)
struct buffer {CVI_U64 phy;void *memory;unsigned bytes;} buffers[2];
static unsigned allocated;
static void cleanup(void){for(unsigned i=0;i<allocated;++i)CVI_SYS_IonFree(buffers[i].phy,buffers[i].memory);}
static void alloc(struct buffer *b,unsigned n){b->bytes=n;if(CVI_SYS_IonAlloc_Cached(&b->phy,&b->memory,"c906_ive_test",n)||!b->memory||b->phy>0x8ff40000u-n){fprintf(stderr,"ION allocation failed\n");exit(1);}++allocated;memset(b->memory,0xa5,n);}
static void flush(struct buffer*b){if(CVI_SYS_IonFlushCache(b->phy,b->memory,b->bytes))exit(2);}
static void invalidate(struct buffer*b){if(CVI_SYS_IonInvalidateCache(b->phy,b->memory,b->bytes))exit(3);}
static double now(clockid_t id){struct timespec t;clock_gettime(id,&t);return t.tv_sec*1000.+t.tv_nsec/1e6;}
static int compare(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return(x>y)-(x<y);}
int main(int argc,char**argv){
 if(argc!=2 && argc<6){fprintf(stderr,"usage: probe read | hist|st|st-linux width height input.raw repeats [padding=0] [reference.bin]\n");return 2;}
 int read=argc==2&&!strcmp(argv[1],"read"),st=!strcmp(argv[1],"st")||!strcmp(argv[1],"st-linux"),linux_mode=!strcmp(argv[1],"st-linux");
 if(argc==2&&!read)return 2;
 if(!read && !st && strcmp(argv[1],"hist"))return 2;
 unsigned width=read?640:atoi(argv[2]),height=read?480:atoi(argv[3]),repeats=read?1:atoi(argv[5]),padding=argc>6?atoi(argv[6]):0;
 unsigned stride=width+(padding?36:0),source_off=FRONT_RAW_OFF+(padding?18*stride+18:0),bytes=width*height;
 if(width<32||width>640||height<32||height>480||!repeats||repeats>64||padding>2||(padding==2&&(!st||linux_mode||(width&7)))||source_off+height*stride>FRONT_INPUT_MASK_OFF)return 2;
 atexit(cleanup);alloc(&buffers[0],FRONT_BYTES);alloc(&buffers[1],IVE_ARENA_BYTES);
 unsigned char *base=buffers[0].memory,*arena=buffers[1].memory,*source=base+source_off;
 unsigned char *input=malloc(bytes),*expected=malloc(bytes);uint32_t hist[256]={0};
 if(!read){FILE*f=fopen(argv[4],"rb");if(!f || fread(input,1,bytes,f)!=bytes)return 5;fclose(f);
  for(unsigned y=0;y<height;++y)memcpy(source+y*stride,input+y*width,width);
  for(unsigned i=0;i<bytes;++i)++hist[input[i]];
 }
 unsigned index=st?2:0,destination=st?IVE_ST_OUT_OFF:IVE_HIST_OFF;
 struct ive_program *program=(void*)(arena+IVE_PROGRAM_OFF+index*IVE_PROGRAM_BYTES);
 if(!read && ive_make_program(program,st,width,height,padding==2?width:stride,padding==2?buffers[1].phy+IVE_PACK_OFF:buffers[0].phy+source_off,buffers[1].phy+destination,buffers[1].phy+IVE_SCRATCH_OFF,25))return 6;
 struct tpu_probe_control *ctl=(void*)base;memset(ctl,0,sizeof(*ctl));ctl->magic=TPU_PROBE_MAGIC;ctl->version=TPU_PROBE_VERSION;ctl->operation=IVE_PROBE_OPERATION;
 struct ive_control *c=(void*)(base+IVE_META_OFF);memset(c,0,sizeof(*c));c->magic=IVE_MAGIC;c->version=IVE_VERSION;c->arena=buffers[1].phy;c->arena_bytes=IVE_ARENA_BYTES;c->flags=read?0:st?IVE_FLAG_ST:IVE_FLAG_HIST;c->quality=25;c->width=width;c->height=height;c->stride=stride;c->test_program=read?~0u:index;c->repeats=repeats;
 c->pack_input=padding==2;
 flush(&buffers[0]);flush(&buffers[1]);int fd=open(linux_mode?"/dev/cvi-ive":"/dev/c906-tpu-probe",O_RDWR);if(fd<0){perror("open");return 7;}
 double begin=now(CLOCK_MONOTONIC),cpu=now(CLOCK_THREAD_CPUTIME_ID);int ret;
 if(linux_mode){struct cvi_ive_ioctl_stcandicorner arg={0};arg.stSrc.enType=IVE_IMAGE_TYPE_U8C1;arg.stSrc.u32Width=width;arg.stSrc.u32Height=height;arg.stSrc.u32Stride[0]=stride;arg.stSrc.u64PhyAddr[0]=buffers[0].phy+source_off;arg.stSrc.u64VirAddr[0]=(uintptr_t)source;
  arg.stDst=arg.stSrc;arg.stDst.u32Stride[0]=width;arg.stDst.u64PhyAddr[0]=buffers[1].phy+destination;arg.stDst.u64VirAddr[0]=(uintptr_t)(arena+destination);
  arg.stCtrl.stMem.u64PhyAddr=buffers[1].phy+IVE_SCRATCH_OFF;arg.stCtrl.stMem.u64VirAddr=(uintptr_t)(arena+IVE_SCRATCH_OFF);arg.stCtrl.stMem.u32Size=IVE_SCRATCH_BYTES;arg.stCtrl.u0q8QualityLevel=25;arg.bInstant=1;
  ret=ioctl(fd,CVI_IVE_IOC_STCandiCorner,&arg);
 }else{uint32_t phys=buffers[0].phy;ret=ioctl(fd,IVE_IOCTL,&phys);}
 double elapsed=now(CLOCK_MONOTONIC)-begin,cp=now(CLOCK_THREAD_CPUTIME_ID)-cpu;close(fd);invalidate(&buffers[0]);invalidate(&buffers[1]);
 printf("mode=%s width=%u height=%u stride=%u program=%u ioctl=%d status=%d completed=%u caps=%u last_reg=%x last_value=%x wall_ms=%.6f cpu_ms=%.6f\n",argv[1],width,height,stride,read?0:program->count,ret,ctl->status,ctl->completed,c->capabilities,c->last_register,c->last_value,elapsed,cp);
 if(ret){perror("IVE call");return 8;}if(!linux_mode && (ctl->status||ctl->completed!=repeats))return 9;
 unsigned errors=0;
 /* DMA may only touch the requested output and the reserved ST workspace.
  * Check the complete unused gaps, not just a word after the destination. */
 if(!read){
  unsigned lo=st?IVE_SCRATCH_OFF+IVE_SCRATCH_BYTES:IVE_HIST_OFF+1024;
  unsigned hi=st?IVE_ST_OUT_OFF:IVE_SCRATCH_OFF;
  unsigned corrupt=0;
  for(unsigned i=lo;i<hi;++i)corrupt+=arena[i]!=0xa5;
  lo=st?IVE_ST_OUT_OFF+bytes:IVE_ST_OUT_OFF;
  for(unsigned i=lo;i<IVE_ARENA_BYTES;++i){if(padding==2 && i>=IVE_PACK_OFF && i<IVE_PACK_OFF+bytes)continue;corrupt+=arena[i]!=0xa5;}
  unsigned main_guard=source_off+height*stride;
  for(unsigned i=main_guard;i<main_guard+4096 && i<FRONT_INPUT_MASK_OFF;++i)corrupt+=base[i]!=0xa5;
  printf("dma_guard_errors=%u\n",corrupt);errors+=corrupt;
 }
 if(!read && !st){uint32_t*result=(void*)(arena+destination);for(unsigned i=0;i<256;++i)if(result[i]!=hist[i]){if(errors<4)printf("bin=%u got=%u expected=%u\n",i,result[i],hist[i]);++errors;}}
 if(st){unsigned nonzero=0;for(unsigned i=0;i<bytes;++i)nonzero+=arena[destination+i]!=0;printf("nonzero=%u\n",nonzero);
  if(argc>7){FILE*f=fopen(argv[7],linux_mode?"wb":"rb");if(!f)return 10;
   if(linux_mode){if(fwrite(arena+destination,1,bytes,f)!=bytes)return 11;}
   else{if(fread(expected,1,bytes,f)!=bytes)return 12;for(unsigned i=0;i<bytes;++i)if(expected[i]!=arena[destination+i]){if(errors<4)printf("pixel=%u got=%u expected=%u\n",i,arena[destination+i],expected[i]);++errors;}}
   fclose(f);
  }
 }
 if(!read&&!linux_mode){uint64_t total=0;for(unsigned i=0;i<repeats;++i)total+=c->run_ticks[i];qsort(c->run_ticks,repeats,sizeof(uint64_t),compare);printf("C906 device_ms mean=%.6f p95=%.6f max=%.6f pack_mean_ms=%.6f errors=%u\n",total/25000./repeats,c->run_ticks[(repeats-1)*95/100]/25000.,c->run_ticks[repeats-1]/25000.,c->pack_ticks/25000./repeats,errors);}
 free(input);free(expected);return errors?13:0;
}

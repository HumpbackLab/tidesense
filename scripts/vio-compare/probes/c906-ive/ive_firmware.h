/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only C906 executes IVE register programs. A53 builds them once at startup.
 */
#include "shared.h"
#include "../c906-frontend/front_shared.h"
#define C906_IVE_BASE 0x0a0a0000ul
static unsigned ive_capabilities(void){unsigned f=0;
#ifdef FRONT_IVE_HIST
 f|=IVE_FLAG_HIST;
#endif
#ifdef FRONT_IVE_ST
 f|=IVE_FLAG_ST;
#endif
 return f;
}
static struct ive_control *ive_bind(uintptr_t base){
 struct ive_control *c=(void*)(base+IVE_META_OFF);inv_dcache_range((uintptr_t)c,sizeof(*c));
 if(c->magic!=IVE_MAGIC || c->version!=IVE_VERSION || c->arena<0x80000000u || c->arena>0x8ff40000u-IVE_ARENA_BYTES || c->arena_bytes!=IVE_ARENA_BYTES)return 0;
 if(c->pack_input && (c->pack_input!=1 || c->test_program!=2 || !c->width || c->width>FRONT_MAX_WIDTH || (c->width&7) || !c->height || c->height>FRONT_MAX_HEIGHT || c->stride!=c->width+36))return 0;
 c->capabilities=ive_capabilities();c->status=0;
 if(c->flags&~c->capabilities){c->status=-81;return 0;}
 return c;
}
static void ive_abort(void){
 probe_write(C906_IVE_BASE+4,3);uint64_t t=probe_clock();while(probe_clock()-t<250u){}
 probe_write(C906_IVE_BASE+4,2);probe_write(C906_IVE_BASE+0x94,0);
}
static int ive_execute_program(struct ive_control *c,unsigned index){
 if(index>=IVE_PROGRAMS || !(c->flags&(index<2?IVE_FLAG_HIST:IVE_FLAG_ST)))return -82;
 struct ive_program *p=(void*)((uintptr_t)c->arena+IVE_PROGRAM_OFF+index*IVE_PROGRAM_BYTES);
 inv_dcache_range((uintptr_t)p,64);unsigned count=p->count;
 if(!count || count>sizeof(p->commands)/sizeof(p->commands[0]))return -83;
 inv_dcache_range((uintptr_t)p->commands,count*sizeof(p->commands[0]));
 /* Reject the entire stream before writing any MMIO. In particular, a bad
  * trailing command must not leave an earlier DMA operation running. */
 for(unsigned i=0;i<count;++i){
  unsigned off=p->commands[i].offset,val=p->commands[i].value;
  if(off==IVE_CMD_NORM)continue;
  if(off==IVE_CMD_CLEAR){if(val!=0x90 && val!=0x98)return -84;continue;}
  if(off==IVE_CMD_DELAY){if(val>1000u)return -84;continue;}
  if(off==IVE_CMD_IF_SAT){if(val>count-i-1)return -84;continue;}
  if(off==IVE_CMD_WAIT){if(!val || (val&~0x1ffffffu))return -84;continue;}
  if(off>=0x3100 || (off&3))return -84;
 }
 uint64_t begin=probe_clock();
 for(unsigned i=0;i<count;++i){
  unsigned offset=p->commands[i].offset,value=p->commands[i].value;
  if(offset==IVE_CMD_NORM){unsigned live=probe_read(C906_IVE_BASE+0x138);probe_write(C906_IVE_BASE+0x138,(live&0xffff00feu)|(value&0x0000ff01u));}
  else if(offset==IVE_CMD_CLEAR){if(value!=0x90 && value!=0x98)return -84;unsigned state=probe_read(C906_IVE_BASE+value);probe_write(C906_IVE_BASE+value,state);if(value==0x98)probe_write(C906_IVE_BASE+value,state);}
  else if(offset==IVE_CMD_DELAY){if(value>1000u)return -84;uint64_t t=probe_clock();while(probe_clock()-t<value*25u){}}
  else if(offset==IVE_CMD_IF_SAT){if(value>count-i-1)return -84;if((probe_read(C906_IVE_BASE+0x2388)&65535u)!=65535u)i+=value;}
  else if(offset==IVE_CMD_WAIT){
   uint64_t t=probe_clock();unsigned state;
   while(!((state=probe_read(C906_IVE_BASE+0x90))&value)){
    if(probe_clock()-t>500000u){c->last_register=0x90;c->last_value=state;ive_abort();return -85;}
   }
  }else if(offset<0x3100 && !(offset&3))probe_write(C906_IVE_BASE+offset,value);
  else return -84;
  if(probe_clock()-begin>2500000u){ive_abort();return -86;}
 }
 if(index<2){c->histogram_ticks+=probe_clock()-begin;++c->histogram_calls;}
 else{c->st_ticks+=probe_clock()-begin;++c->st_calls;}
 return 0;
}
/* The pyramid's 18-pixel halo makes each row unaligned. Avoid RV64 unaligned
 * loads: join adjacent aligned words, using the existing halo for the tail. */
static void ive_pack_input(uintptr_t base,struct ive_control *c){
 typedef uint64_t alias_word __attribute__((may_alias));
 uint64_t begin=probe_clock();
 uintptr_t source=base+FRONT_RAW_OFF+18*c->stride+18;
 uintptr_t destination=(uintptr_t)c->arena+IVE_PACK_OFF;
 inv_dcache_range(source&~63ul,((source+c->height*c->stride+63)&~63ul)-(source&~63ul));
 for(unsigned y=0;y<c->height;++y){
  uintptr_t row=source+y*c->stride;unsigned shift=(row&7)*8;
  const alias_word *in=(void*)(row&~7ul);alias_word *out=(void*)(destination+y*c->width);
  for(unsigned x=0;x<c->width/8;++x)out[x]=shift?(in[x]>>shift)|(in[x+1]<<(64-shift)):in[x];
 }
 clean_dcache_range(destination,c->width*c->height);
 c->pack_ticks+=probe_clock()-begin;
}
static void c906_ive_probe(uintptr_t base){
 struct tpu_probe_control *ctl=(void*)base;struct ive_control *c=ive_bind(base);
 ctl->status=-80;ctl->completed=0;
 if(!c)goto done;
 if(c->test_program==~0u){c->last_register=0;c->last_value=probe_read(C906_IVE_BASE);ctl->status=0;ctl->completed=1;goto done;}
 if(!c->repeats || c->repeats>64)goto done;
 for(unsigned i=0;i<c->repeats;++i){uint64_t t=probe_clock();if(c->pack_input)ive_pack_input(base,c);ctl->status=ive_execute_program(c,c->test_program);c->run_ticks[i]=probe_clock()-t;if(ctl->status)break;++ctl->completed;}
 c->completed=ctl->completed;c->status=ctl->status;
 done:
 clean_dcache_range(base+IVE_META_OFF,sizeof(struct ive_control));clean_dcache_range(base,TPU_PROBE_CONTROL_BYTES);
}

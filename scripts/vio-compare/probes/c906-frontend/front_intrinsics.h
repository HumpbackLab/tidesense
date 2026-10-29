/* Latest completed calibration, independent of camera/frame handshakes. */
#ifndef TIDESENSE_FRONT_INTRINSICS_H
#define TIDESENSE_FRONT_INTRINSICS_H
#include "../c906-tpu/shared.h"
#include <stddef.h>
#define FRONT_INTRINSICS_OFF 1015808u
#define FRONT_INTRINSICS_MAGIC 0x46434131u
#define FRONT_INTRINSICS_WORDS 21u
/* words[0]: publication version; camera c: words[1+10*c] model (1=radtan,
 * 2=equi), words[2+10*c] width|height<<32, then 8 raw float64 parameters. */
struct front_intrinsics_data { probe_u64 words[FRONT_INTRINSICS_WORDS]; };
struct front_intrinsics_slot {
    probe_u32 sequence;
    unsigned char reserved[60];
    struct front_intrinsics_data data;
    unsigned char padding[24];
};
struct front_intrinsics_channel {
    probe_u32 magic,published;
    unsigned char reserved[56];
    struct front_intrinsics_slot slots[2];
};
typedef void (*front_intrinsics_sync)(void *,unsigned,unsigned);
/* One A53 publisher writes the inactive slot, then publishes its index. The
 * active completed value stays readable during writes. Per-slot generations
 * also detect reuse if two publications overtake a reader. Atomic payload
 * accesses keep the host threaded emulation free from C++ data races. */
static inline void front_intrinsics_publish(void *base,const struct front_intrinsics_data *data,
                                           front_intrinsics_sync clean) {
    struct front_intrinsics_channel *c=(struct front_intrinsics_channel *)((unsigned char *)base+FRONT_INTRINSICS_OFF);
    unsigned index=1u-(__atomic_load_n(&c->published,__ATOMIC_RELAXED)&1u);
    struct front_intrinsics_slot *slot=&c->slots[index];
    unsigned offset=FRONT_INTRINSICS_OFF+offsetof(struct front_intrinsics_channel,slots)+index*sizeof(*slot);
    probe_u32 odd=__atomic_load_n(&slot->sequence,__ATOMIC_RELAXED)+1u;
    __atomic_store_n(&slot->sequence,odd,__ATOMIC_RELEASE);
    if(clean)clean(base,offset,64);
    for(unsigned i=0;i<FRONT_INTRINSICS_WORDS;++i)__atomic_store_n(&slot->data.words[i],data->words[i],__ATOMIC_RELAXED);
    if(clean)clean(base,offset+64,sizeof(slot->data));
    __atomic_store_n(&slot->sequence,odd+1u,__ATOMIC_RELEASE);
    if(clean)clean(base,offset,64);
    __atomic_store_n(&c->magic,FRONT_INTRINSICS_MAGIC,__ATOMIC_RELAXED);
    __atomic_store_n(&c->published,index,__ATOMIC_RELEASE);
    if(clean)clean(base,FRONT_INTRINSICS_OFF,64);
}
/* Read the complete value published when this call starts. Never wait for a
 * frame or an in-progress writer. Leave last intact only if a slot was reused
 * during this read, or calibration has not been published yet. */
static inline int front_intrinsics_read(void *base,struct front_intrinsics_data *last,
                                       front_intrinsics_sync invalidate) {
    struct front_intrinsics_channel *c=(struct front_intrinsics_channel *)((unsigned char *)base+FRONT_INTRINSICS_OFF);
    if(invalidate)invalidate(base,FRONT_INTRINSICS_OFF,64);
    unsigned index=__atomic_load_n(&c->published,__ATOMIC_ACQUIRE)&1u;
    if(__atomic_load_n(&c->magic,__ATOMIC_RELAXED)!=FRONT_INTRINSICS_MAGIC)return 0;
    struct front_intrinsics_slot *slot=&c->slots[index];
    unsigned offset=FRONT_INTRINSICS_OFF+offsetof(struct front_intrinsics_channel,slots)+index*sizeof(*slot);
    if(invalidate)invalidate(base,offset,64);
    probe_u32 before=__atomic_load_n(&slot->sequence,__ATOMIC_ACQUIRE);
    if(before&1u)return 0;
    if(invalidate)invalidate(base,offset+64,sizeof(slot->data));
    struct front_intrinsics_data copy;
    for(unsigned i=0;i<FRONT_INTRINSICS_WORDS;++i)copy.words[i]=__atomic_load_n(&slot->data.words[i],__ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if(invalidate)invalidate(base,offset,64);
    if(before!=__atomic_load_n(&slot->sequence,__ATOMIC_ACQUIRE) || !copy.words[0])return 0;
    *last=copy;return 1;
}
#ifdef __cplusplus
static_assert(offsetof(front_intrinsics_channel,slots)==64 && sizeof(front_intrinsics_slot)==256,"Calibration header needs its own cache line");
#else
_Static_assert(offsetof(struct front_intrinsics_channel,slots)==64 && sizeof(struct front_intrinsics_slot)==256,"Calibration header needs its own cache line");
#endif
#endif

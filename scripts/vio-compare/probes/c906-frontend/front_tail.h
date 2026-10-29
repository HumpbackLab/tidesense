/* Optional one-batch CPU tail protocol. Each processor owns separate cache lines.
 * No cross-architecture compare/exchange or cache-coherent atomics are required. */
#ifndef TIDESENSE_FRONT_TAIL_H
#define TIDESENSE_FRONT_TAIL_H
#include "front_shared.h"
#define FRONT_TAIL_OFF 1024000u
#define FRONT_TAIL_BYTES 1024u
#define FRONT_TAIL_MAGIC 0x54414931u
#define FRONT_TAIL_POINTS 16u
#define FRONT_TAIL_MIN_POINTS 48u
#define FRONT_TAIL_HOST_OFF 0u
#define FRONT_TAIL_REMOTE_OFF 64u
#define FRONT_TAIL_TASK_OFF 128u
#define FRONT_TAIL_RESULT_OFF 640u
#define FRONT_TAIL_IDLE 0u
#define FRONT_TAIL_AVAILABLE 1u
#define FRONT_TAIL_HOST_DONE 2u
#define FRONT_TAIL_HOST_FAILED 3u
#define FRONT_TAIL_RUNNING 0u
#define FRONT_TAIL_OFFERED 1u
#define FRONT_TAIL_FINISHED 2u
struct front_tail_host {
    probe_u32 magic,state;
    probe_u64 timestamp;
    unsigned char padding[48];
};
struct front_tail_remote {
    probe_u32 magic,state;
    probe_u64 timestamp,wait_ticks;
    probe_u32 points;
    int error;
    unsigned char padding[32];
};
struct front_tail_task {
    probe_u64 timestamp;
    probe_u32 first,count,levels,reserved;
    struct lk_tpu_level previous[LK_TPU_LEVELS],current[LK_TPU_LEVELS];
    struct lk_tpu_point input[FRONT_TAIL_POINTS];
};
struct front_tail_result {
    struct lk_tpu_point output[FRONT_TAIL_POINTS];
    unsigned char valid[FRONT_TAIL_POINTS];
};
#define FRONT_TAIL_PTR(type,base,off) ((struct type*)((unsigned char*)(base)+FRONT_TAIL_OFF+(off)))
#ifdef __cplusplus
#define TAIL_ASSERT static_assert
#else
#define TAIL_ASSERT _Static_assert
#endif
TAIL_ASSERT(sizeof(struct front_tail_host)==64,"Host control cache line");
TAIL_ASSERT(sizeof(struct front_tail_remote)==64,"Remote control cache line");
TAIL_ASSERT(FRONT_TAIL_TASK_OFF+sizeof(struct front_tail_task)<=FRONT_TAIL_RESULT_OFF,"Task overlaps CPU output");
TAIL_ASSERT(FRONT_TAIL_RESULT_OFF+sizeof(struct front_tail_result)<=FRONT_TAIL_BYTES,"Tail allocation overflow");
TAIL_ASSERT(FRONT_TAIL_OFF+FRONT_TAIL_BYTES<FRONT_PYR_OFF,"Tail overlaps pyramid");
#undef TAIL_ASSERT
#endif

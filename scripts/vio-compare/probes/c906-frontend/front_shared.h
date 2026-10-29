#ifndef TIDESENSE_FRONT_SHARED_H
#define TIDESENSE_FRONT_SHARED_H
#include "../c906-tpu/lk_shared.h"
#define TPU_PROBE_FRONT 4u
#define FRONT_MAGIC 0x46524e54u
#define FRONT_VERSION 3u
#define FRONT_BYTES (12u*1024u*1024u)
#define FRONT_META_OFF 196608u
#define FRONT_PYR_DMA_OFF 262144u
#define FRONT_PYR_JOB_BYTES 65536u
#define FRONT_STATE_OFF 524288u
#define FRONT_WEIGHTS_OFF 528384u
#define FRONT_LUT_OFF 532480u
#define FRONT_EQ_DMA_OFF 786432u
#define FRONT_SNAPSHOT_OFF 983040u
#define FRONT_PYR_OFF 1048576u
#define FRONT_CAM_BYTES 655360u
#define FRONT_RAW_OFF (4u*1024u*1024u)
#define FRONT_INPUT_MASK_OFF (5u*1024u*1024u)
#define FRONT_MASK_OFF (6u*1024u*1024u)
#define FRONT_SCRATCH_OFF (8u*1024u*1024u)
#define FRONT_MAX_WIDTH 640u
#define FRONT_MAX_HEIGHT 480u
#define FRONT_PIXELS (FRONT_MAX_WIDTH*FRONT_MAX_HEIGHT)
#define FRONT_POINTS 256u
#define FRONT_GRID_MAX 16u
#define FRONT_CANDIDATES 8192u
struct front_point { float x,y,response; probe_u32 reserved; probe_u64 id; };
struct front_patch { probe_u32 offset,word,relative; };
struct front_control {
    probe_u32 magic,version,width,height,histogram;
    probe_u32 num_features,grid_x,grid_y,min_px_dist,threshold;
    probe_u32 input_count[2],count[2],reset,deadline_us;
    probe_u64 timestamp,previous_timestamp,currid;
    struct front_point input[2][FRONT_POINTS],old[2][FRONT_POINTS];
    struct lk_tpu_point output[2][FRONT_POINTS];
    unsigned char valid[2][FRONT_POINTS];
    probe_u32 pyramid_dma_bytes[4],pyramid_patch_count[4];
    struct front_patch pyramid_patch[4][64];
    probe_u32 levels;
    probe_u64 equalize_ticks,pyramid_ticks,detect_ticks,lk_ticks,total_ticks;
    probe_u32 equalize_dma_bytes,equalize_patch_count;
    struct front_patch equalize_patch[128];
    probe_u32 deferred_snapshot;
    probe_u64 snapshot_wait_ticks;
};
/* Separate cache lines: Linux publishes this while C906 owns front_control. */
struct front_snapshot {
    probe_u32 ready,magic;
    probe_u64 timestamp;
    unsigned char reserved[48];
    probe_u32 count[2],num_features;
    probe_u64 currid;
    struct front_point points[2][FRONT_POINTS];
};
struct front_state {
    probe_u32 magic,width,height,histogram;
    probe_u32 valid[2],levels;
    probe_u64 timestamp[2];
    struct lk_tpu_level pyramid[2][2][LK_TPU_LEVELS];
};
struct front_ops {
    probe_u64 (*clock)(void);
    int (*pyramid)(void*,struct front_control*,struct front_state*,unsigned,unsigned);
    int (*flow)(void*,const struct lk_tpu_level*,const struct lk_tpu_level*,
                unsigned,const struct front_point*,unsigned,struct lk_tpu_point*,unsigned char*);
    int (*equalize)(void*,struct front_control*,struct front_state*,unsigned,unsigned,const unsigned char*);
    int (*snapshot)(void*,struct front_control*);
};
#ifdef __cplusplus
extern "C" {
#endif
int front_run(void *base,const struct front_ops *ops);
int front_make_jobs(unsigned char *base,probe_u64 phys,unsigned width,unsigned height);
void front_border(unsigned char *base,const struct lk_tpu_level *level);
int front_cpu_pyramid(void*,struct front_control*,struct front_state*,unsigned,unsigned);
#ifdef __cplusplus
}
#endif
#endif

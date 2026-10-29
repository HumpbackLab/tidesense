#ifndef TIDESENSE_C906_TPU_LK_SHARED_H
#define TIDESENSE_C906_TPU_LK_SHARED_H
#include "shared.h"
#define TPU_PROBE_LK 2u
#define LK_TPU_BYTES (2u * 1024u * 1024u)
#define LK_TPU_VERSION 3u
#define LK_TPU_META_OFF 8192u
#ifndef LK_TPU_BATCH
#define LK_TPU_BATCH 16u
#endif
#define LK_TPU_POINTS 160u
#define LK_TPU_LEVELS 5u
#define LK_TPU_BORDER 18u
#define LK_TPU_W 16u
#define LK_TPU_H 15u
#define LK_TPU_PIXELS (LK_TPU_W * LK_TPU_H)
#define LK_TPU_INIT_DMA_OFF TPU_PROBE_DMA_OFF
#define LK_TPU_ITER_DMA_OFF 65536u
#define LK_TPU_WEIGHTS_OFF 131072u
#define LK_TPU_HESS_OFF 132096u
#define LK_TPU_RHS_OFF 132352u
#define LK_TPU_ROW_MASK_OFF 133120u
#define LK_TPU_COL_MASK_OFF 135168u
#define LK_TPU_KERNEL_X_OFF 137216u
#define LK_TPU_KERNEL_Y_OFF 139264u
#define LK_TPU_PLANES_OFF 196608u
/* Optional appended telemetry; LK v3 offsets remain backwards compatible. */
#define LK_TPU_TRACE_MAGIC 0x4c4b5452u
#define LK_TPU_TRACE_OFF 147456u
#define LK_TPU_TRACE_MAX 2048u
struct lk_tpu_trace_span { probe_u64 begin, end; };
struct lk_tpu_point { float x, y; };
struct lk_tpu_level {
    probe_u32 width, height, row_bytes;
    probe_u32 planes[2]; /* native U8 previous and next pyramid levels */
};
struct lk_tpu_control {
    probe_u32 version, count, levels, max_iterations, batch;
    probe_u32 dma_bytes[2];
    probe_u32 init_patch[3][LK_TPU_BATCH], iter_patch[LK_TPU_BATCH];
    struct lk_tpu_level level[LK_TPU_LEVELS];
    struct lk_tpu_point input[LK_TPU_POINTS], initial[LK_TPU_POINTS];
    struct lk_tpu_point output[LK_TPU_POINTS];
    unsigned char valid[LK_TPU_POINTS];
    probe_u32 iterations[LK_TPU_POINTS];
    probe_u64 prepare_ticks, execute_ticks, solve_ticks, total_ticks;
    probe_u32 submissions, valid_count;
    probe_u32 trace_flags, trace_magic, trace_count, trace_overflow;
    probe_u64 trace_begin, trace_end;
};
#ifndef __KERNEL__
static inline unsigned short lk_tpu_bf16(float f)
{
    union { float f; probe_u32 u; } v = { .f = f };
    v.u += 0x7fffu + ((v.u >> 16) & 1u);
    return v.u >> 16;
}
static inline float lk_tpu_float(unsigned short b)
{
    union { float f; probe_u32 u; } v;
    v.u = (probe_u32)b << 16;
    return v.f;
}
#endif
#endif

/* Optional telemetry in the existing control block; frontend v3 ABI is unchanged. */
#ifndef TIDESENSE_FRONT_DETAIL_H
#define TIDESENSE_FRONT_DETAIL_H
#include "../c906-tpu/lk_shared.h"
#include <stddef.h>
#define FRONT_DETAIL_MAGIC 0x46504431u
#define FRONT_DETAIL_TIMES(X) \
 X(mask) X(fast) X(nms) X(sort) X(refine) X(histogram) X(lut) X(border) \
 X(lk_planes) X(lk_prepare) X(lk_execute) X(lk_solve) X(tpu_setup) X(tpu_wait)
#define FRONT_DETAIL_COUNTS(X) X(lk_calls) X(lk_submissions) X(lk_iterations) X(fast_candidates) X(intrinsics_version) X(lk_roi_cache_hits) X(lk_roi_cache_misses) X(lk_dual_packets) X(lk_dual_solo_packets)
struct front_detail {
    probe_u64 magic,flags;
#define MEMBER(n) probe_u64 n##_ticks;
    FRONT_DETAIL_TIMES(MEMBER)
#undef MEMBER
#define MEMBER(n) probe_u64 n;
    FRONT_DETAIL_COUNTS(MEMBER)
#undef MEMBER
};
/* Leave clock synchronization's run_ticks[0] untouched. This range ends before LK metadata. */
#define FRONT_DETAIL_OFF (offsetof(struct tpu_probe_control,run_ticks)+800u*sizeof(probe_u64))
static inline struct front_detail *front_detail_at(void *base) {
    return (struct front_detail *)((unsigned char*)base+FRONT_DETAIL_OFF);
}
#ifdef __cplusplus
static_assert(FRONT_DETAIL_OFF+sizeof(front_detail)<=LK_TPU_META_OFF,"Telemetry overlaps LK metadata");
#else
_Static_assert(FRONT_DETAIL_OFF+sizeof(struct front_detail)<=LK_TPU_META_OFF,"Telemetry overlaps LK metadata");
#endif
#endif

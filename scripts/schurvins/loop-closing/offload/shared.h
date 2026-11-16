/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TIDESENSE_LOOP_MATCH_SHARED_H
#define TIDESENSE_LOOP_MATCH_SHARED_H
#include <stdint.h>
#define LOOP_MATCH_OPERATION 8u
#define LOOP_MATCH_MAGIC 0x4c504d31u
#define LOOP_MATCH_BATCH 1024u
#define LOOP_MATCH_QUERIES 1024u
#define LOOP_MATCH_META_OFF 32768u
#define LOOP_MATCH_LUT_OFF 65536u
#define LOOP_MATCH_A_OFF 131072u
#define LOOP_MATCH_B_OFF 196608u
#define LOOP_MATCH_DIST_OFF 262144u
#define LOOP_MATCH_PAIRS_OFF 270336u
#define LOOP_MATCH_RESULT_OFF 286720u
struct loop_match_pair { uint16_t query, candidate; };
struct loop_match_result { uint16_t distance, candidate; };
struct loop_match_meta {
    uint32_t magic, version, pairs, queries;
    uint32_t use_tpu, tpu_calls, reductions, reserved;
    uint64_t tpu_ticks, reduction_ticks;
};
/* Every integer from 0 through 256 is exactly representable in BF16. */
static inline int loop_match_reduce(void *base) {
    struct loop_match_meta *m=(struct loop_match_meta*)((uint8_t*)base+LOOP_MATCH_META_OFF);
    const uint16_t *dist=(const uint16_t*)((uint8_t*)base+LOOP_MATCH_DIST_OFF);
    const struct loop_match_pair *pairs=(const struct loop_match_pair*)((uint8_t*)base+LOOP_MATCH_PAIRS_OFF);
    struct loop_match_result *best=(struct loop_match_result*)((uint8_t*)base+LOOP_MATCH_RESULT_OFF);
    for(uint32_t i=0;i<m->pairs;++i) {
        if(pairs[i].query>=m->queries || pairs[i].candidate>=LOOP_MATCH_QUERIES || dist[i]>0x4380u) return -23;
        union {uint32_t u;float f;} value; value.u=(uint32_t)dist[i]<<16;
        const unsigned d=(unsigned)value.f;
        struct loop_match_result *r=best+pairs[i].query;
        if(d<r->distance || (d==r->distance && pairs[i].candidate<r->candidate)) {
            r->distance=d;r->candidate=pairs[i].candidate;
        }
    }
    m->reductions=m->pairs;
    return 0;
}
#endif

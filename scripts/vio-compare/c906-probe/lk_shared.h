#ifndef TIDESENSE_C906_LK_SHARED_H
#define TIDESENSE_C906_LK_SHARED_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 uint8_t;
typedef u32 uint32_t;
typedef u64 uint64_t;
#else
#include <stdint.h>
#endif

#define LK_PROBE_CMD 0x4f
#define LK_PROBE_MAGIC 0x4c4b3930u
#define LK_PROBE_WIDTH 640u
#define LK_PROBE_HEIGHT 400u
#define LK_PROBE_POINTS 200u
#define LK_PROBE_LEVELS 5u
#define LK_PROBE_IMAGE_BYTES (LK_PROBE_WIDTH * LK_PROBE_HEIGHT)
#define LK_PROBE_PYRAMID_BYTES 341000u
#define LK_PROBE_CONTROL_BYTES 4096u
#define LK_PROBE_PREV_OFF LK_PROBE_CONTROL_BYTES
#define LK_PROBE_NEXT_OFF (LK_PROBE_PREV_OFF + LK_PROBE_IMAGE_BYTES)
#define LK_PROBE_PYR_PREV_OFF (LK_PROBE_NEXT_OFF + LK_PROBE_IMAGE_BYTES)
#define LK_PROBE_PYR_NEXT_OFF (LK_PROBE_PYR_PREV_OFF + LK_PROBE_PYRAMID_BYTES)
#define LK_PROBE_BUFFER_BYTES (LK_PROBE_PYR_NEXT_OFF + LK_PROBE_PYRAMID_BYTES)

struct lk_probe_point {
    float x;
    float y;
};

struct lk_probe_control {
    uint32_t magic;
    uint32_t count;
    uint32_t status;
    uint32_t valid;
    uint64_t pyramid_ticks;
    uint64_t track_ticks;
    uint64_t total_ticks;
    uint32_t total_iterations;
    uint32_t reserved;
    struct lk_probe_point input[LK_PROBE_POINTS];
    struct lk_probe_point output[LK_PROBE_POINTS];
    uint8_t mask[LK_PROBE_POINTS];
};

typedef uint64_t (*lk_probe_clock_fn)(void);
int lk_probe_run(void *buffer, lk_probe_clock_fn clock_fn);

#endif
